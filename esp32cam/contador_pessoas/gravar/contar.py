"""
contar.py - prototipo do contador de pessoas (roda no PC sobre os clipes gravados)

Uso:
    python contar.py clips                  # processa todos os clipes e mostra a tabela
    python contar.py clips/entra_1 --ver    # um clipe, com janela
                                            #   q = fecha a janela, espaco = pausa

Cada clipe e uma pasta com 00000.png, 00001.png, ... e gabarito.csv (gerados pelo gravar.py).
O esperado vem de esperado.txt na pasta do clipe (ex.: "1 0" = 1 entrada e 0 saidas),
se existir, ou da contagem de linhas do gabarito.csv.

Logica: subtracao de fundo -> blobs -> rastreamento por vizinho mais proximo ->
cruzamento de UMA linha diagonal com zona morta (histerese).
  lado da porta -> lado da sala  = entrada
  lado da sala  -> lado da porta = saida
"""
import argparse
import csv
import glob
import os
import itertools

import cv2
import numpy as np

# ======================= PARAMETROS =======================
ESCALA = 0.25         # Reduz para 80x60 para caber na RAM da ESP32
P1 = (0.00, 0.00)     # Mantém a proporção da linha diagonal
P2 = (1.00, 1.00)     
PONTO_PORTA = (0.20, 0.05)   

BANDA = 3            # Zona morta proporcional à baixa resolução
LIMIAR = 20           # Sensibilidade de diferença de luz
ALFA_BG = 0.005      
AREA_MIN = 50         # Blocos menores já contam como pessoa na resolução 80x60
DIST_MAX = 24         # Deslocamento curto em baixa resolução
FALTAS_MAX = 6        
AQUECIMENTO = 15      
LUZ_GLOBAL = 0.75     
AREA_PESSOA = 1100
# ========================================================================

FONT = cv2.FONT_HERSHEY_SIMPLEX

# Variáveis globais para serem injetadas pela busca em grade
GLOBAL_PARAMS = {}


def ler_esperado(pasta):
    txt = os.path.join(pasta, "esperado.txt")
    if os.path.exists(txt):
        with open(txt) as f:
            e, s = f.read().split()[:2]
        return int(e), int(s)
    ent = sai = 0
    csv_path = os.path.join(pasta, "gabarito.csv")
    if os.path.exists(csv_path):
        with open(csv_path, newline="") as f:
            for row in csv.DictReader(f):
                if row["evento"] == "entrada":
                    ent += 1
                elif row["evento"] == "saida":
                    sai += 1
    return ent, sai


def processar(pasta, ver=False):
    # Declaramos explicitamente que vamos usar as variáveis globais
    global BANDA, LIMIAR, ALFA_BG, AREA_MIN, DIST_MAX, FALTAS_MAX , AREA_PESSOA
    
    # Se a busca em grade estiver rodando, sobrescrevemos os parâmetros locais
    if GLOBAL_PARAMS:
        BANDA = GLOBAL_PARAMS.get("BANDA", BANDA)
        LIMIAR = GLOBAL_PARAMS.get("LIMIAR", LIMIAR)
        ALFA_BG = GLOBAL_PARAMS.get("ALFA_BG", ALFA_BG)
        AREA_MIN = GLOBAL_PARAMS.get("AREA_MIN", AREA_MIN)
        DIST_MAX = GLOBAL_PARAMS.get("DIST_MAX", DIST_MAX)
        FALTAS_MAX = GLOBAL_PARAMS.get("FALTAS_MAX", FALTAS_MAX)
        AREA_PESSOA = GLOBAL_PARAMS.get("AREA_PESSOA", AREA_PESSOA)

    arqs = sorted(glob.glob(os.path.join(pasta, "*.png")))
    if not arqs:
        return None

    g0 = cv2.imread(arqs[0], cv2.IMREAD_GRAYSCALE)
    H, W = g0.shape
    w, h = int(W * ESCALA), int(H * ESCALA)
    esc = 1.0 / ESCALA

    # --- geometria da linha ---
    p1 = np.array([P1[0] * w, P1[1] * h])
    p2 = np.array([P2[0] * w, P2[1] * h])
    v = p2 - p1
    normal = np.array([-v[1], v[0]]) / np.linalg.norm(v)

    def dist(p):
        return float(np.dot(normal, np.array(p, dtype=float) - p1))

    sinal_porta = np.sign(dist((PONTO_PORTA[0] * w, PONTO_PORTA[1] * h)))
    if sinal_porta == 0:
        raise SystemExit("PONTO_PORTA esta em cima da linha: escolha outro ponto.")

    def lado(p, anterior):
        """-1 = lado da porta, +1 = lado da sala, 'anterior' se estiver na zona morta."""
        d = dist(p)
        if abs(d) <= BANDA:
            return anterior
        return -1 if np.sign(d) == sinal_porta else 1

    # --- estado ---
    bg = None
    acc = []
    tracks = []
    prox_id = 0
    ent = sai = 0
    mask = np.zeros((h, w), np.uint8)
    kernel = np.ones((3, 3), np.uint8)

    mask = np.zeros((h, w), np.uint8)
    kernel = np.ones((3, 3), np.uint8)

    # --- MÁSCARA DA ZONA DA PORTA (Canto superior direito) ---
    # Define um retângulo onde a folha da porta se move: X de 68% a 100%, Y de 0% a 45%
    zona_porta = np.zeros((h, w), dtype=bool)
    zona_porta[0:int(h * 0.45), int(w * 0.68):w] = True

    for i, arq in enumerate(arqs):
        gray = cv2.imread(arq, cv2.IMREAD_GRAYSCALE)
        pequeno = cv2.resize(gray, (w, h), interpolation=cv2.INTER_AREA)
        g = cv2.GaussianBlur(pequeno, (5, 5), 0).astype(np.float32)

        if i < AQUECIMENTO:
            acc.append(g)
            if i == AQUECIMENTO - 1:
                bg = np.median(acc, axis=0).astype(np.float32)
        else:
            bruto = (np.abs(g - bg) > LIMIAR).astype(np.uint8)

            # 1. Corta a folha da porta no canto superior direito
            bruto[zona_porta] = 0
            bg[zona_porta] += (ALFA_BG * 5.0) * (g[zona_porta] - bg[zona_porta])

            # 2. Atualiza o fundo onde não há movimento (livre)
            # e MUITO suavemente onde há mancha (~livre) para apagar luz da porta sem criar fantasma no chão!
            livre = bruto == 0
            bg[livre] += ALFA_BG * (g[livre] - bg[livre])
            bg[~livre] += (ALFA_BG * 0.35) * (g[~livre] - bg[~livre])

            mask = cv2.morphologyEx(bruto, cv2.MORPH_OPEN, kernel)
            mask = cv2.dilate(mask, kernel, iterations=2)
            n, _, stats, cents = cv2.connectedComponentsWithStats(mask, connectivity=8)
            
            blobs = [(cents[j], stats[j, cv2.CC_STAT_AREA])
                     for j in range(1, n) if stats[j, cv2.CC_STAT_AREA] >= AREA_MIN]

            # 3. Associação com Inércia (projeta a posição esperada do rastro para filas não trocarem de ID)
            pares = []
            for ti, t in enumerate(tracks):
                esp_x = t["pos"][0] + t.get("vx", 0.0) * 0.5
                esp_y = t["pos"][1] + t.get("vy", 0.0) * 0.5
                for bi, (c, a) in enumerate(blobs):
                    d = np.hypot(c[0] - esp_x, c[1] - esp_y)
                    if d < DIST_MAX:
                        pares.append((d, ti, bi))
            
            pares.sort(key=lambda x: x[0])
            tracks_usados = set()
            blobs_usados = set()

            for d, ti, bi in pares:
                if ti in tracks_usados or bi in blobs_usados:
                    continue
                tracks_usados.add(ti)
                blobs_usados.add(bi)
                
                t = tracks[ti]
                c, a = blobs[bi]
                
                # Atualiza vetor de velocidade (direção de caminhada)
                t["vx"] = 0.5 * t.get("vx", 0.0) + 0.5 * (float(c[0]) - t["pos"][0])
                t["vy"] = 0.5 * t.get("vy", 0.0) + 0.5 * (float(c[1]) - t["pos"][1])
                t["pos"] = (float(c[0]), float(c[1]))
                t["area"] = 0.7 * t["area"] + 0.3 * float(a)
                t["faltas"] = 0
                t["idade"] = t.get("idade", 0) + 1
                if t.get("cd", 0) > 0:
                    t["cd"] -= 1
                
                novo = lado(t["pos"], t["lado"])
                if t["lado"] != 0 and novo != 0 and novo != t["lado"] and t.get("cd", 0) == 0:
                    qtd = max(1, round(t["area"] / AREA_PESSOA)) if AREA_PESSOA else 1
                    if novo == 1:      # porta -> sala
                        ent += qtd
                    else:              # sala -> porta
                        sai += qtd
                    t["ja_contou"] = True
                    t["cd"] = 8        # Impede contar entrada falsa logo após sair (e vice-versa)
                t["lado"] = novo

            for ti, t in enumerate(tracks):
                if ti not in tracks_usados:
                    # CORREÇÃO DA FILA: Se o rastro JÁ CONTOU e sumiu na porta, mata ele rápido
                    # para ele não ficar parado na porta e "roubar" a próxima pessoa da fila!
                    if t.get("ja_contou", False):
                        t["faltas"] += 3
                    else:
                        t["faltas"] += 1

            for bi, (c, a) in enumerate(blobs):
                if bi not in blobs_usados:
                    pos = (float(c[0]), float(c[1]))
                    d_ini = dist(pos)
                    lado_ini = -1 if np.sign(d_ini) == sinal_porta else 1
                    tracks.append({"id": prox_id, "pos": pos, "area": float(a),
                                   "faltas": 0, "idade": 1, "vx": 0.0, "vy": 0.0,
                                   "cd": 0, "ja_contou": False, "lado": lado_ini})
                    prox_id += 1

            tracks = [t for t in tracks if t["faltas"] <= FALTAS_MAX]

        if ver:
            vis = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
            cv2.line(vis, tuple((p1 * esc).astype(int)), tuple((p2 * esc).astype(int)),
                     (0, 255, 255), 1)
            # Desenha em vermelho a caixa da porta que está sendo ignorada
            cv2.rectangle(vis, (int(W * 0.68), 0), (W, int(H * 0.45)), (0, 0, 255), 1)
            for t in tracks:
                cx, cy = int(t["pos"][0] * esc), int(t["pos"][1] * esc)
                cv2.circle(vis, (cx, cy), 5, (0, 255, 0), -1)
                cv2.putText(vis, str(t["id"]), (cx + 7, cy), FONT, 0.4, (0, 255, 0), 1)
            cv2.putText(vis, f"E:{ent} S:{sai}  quadro {i}", (5, 15), FONT, 0.45, (0, 255, 0), 1)
            m = cv2.resize(mask * 255, (W, H), interpolation=cv2.INTER_NEAREST)
            cv2.imshow("contar  (esq: video | dir: movimento)",
                       np.hstack([vis, cv2.cvtColor(m, cv2.COLOR_GRAY2BGR)]))
            k = cv2.waitKey(80) & 0xFF
            if k == ord(" "):
                cv2.waitKey(0)
            elif k == ord("q"):
                ver = False
                cv2.destroyAllWindows()

    if ver:
        cv2.destroyAllWindows()
    return ent, sai


def busca_em_grade(pastas):
    """
    Executa combinações de parâmetros para achar a melhor taxa de acerto.
    """
    global GLOBAL_PARAMS

    # ====================================================================
    # DEFINA AQUI OS VALORES QUE VOCÊ QUER TESTAR NA BUSCA EM GRADE
    # Adicione novos valores dentro das listas abaixo.
    # ====================================================================
    grade = {
        "BANDA": [3, 4],
        "LIMIAR": [20, 22,24],
        "AREA_MIN": [35,40,50],          # Ignora ruídos pequenos na escala 0.25
        "DIST_MAX": [24, 26,30],          # CRÍTICO: evita quebrar o rastro quando a pessoa anda rápido
        "FALTAS_MAX": [6],
        "ALFA_BG": [0.005, 0.015],
        "AREA_PESSOA": [900, 1100,1200]
    }
    
    chaves = list(grade.keys())
    valores = list(grade.values())
    combinacoes = list(itertools.product(*valores))
    
    melhor_acerto = -1
    melhor_parametro = {}
    melhor_log_saida = ""
    
    print(f"Iniciando busca em grade para {len(combinacoes)} combinações...\n")
    
    for i, comb in enumerate(combinacoes):
        params_atuais = dict(zip(chaves, comb))
        GLOBAL_PARAMS = params_atuais
        
        acertos = 0
        total = 0
        log_temporario = f"--- Testando Parâmetros: {params_atuais} ---\n"
        log_temporario += f"{'clipe':<24}{'esperado E/S':>14}{'contado E/S':>14}  resultado\n"
        
        for p in pastas:
            r = processar(p, False)
            if r is None:
                continue
            ent, sai = r
            ee, es = ler_esperado(p)
            ok = (ent, sai) == (ee, es)
            acertos += ok
            total += 1
            
            status = 'OK' if ok else 'ERRO'
            log_temporario += f"{os.path.basename(p):<24}{f'{ee}/{es}':>14}{f'{ent}/{sai}':>14}  {status}\n"
        
        log_temporario += f"\nacertos: {acertos}/{total}\n"
        
        print(f"[{i+1}/{len(combinacoes)}] {params_atuais} -> Acertos: {acertos}/{total}")
        
        if acertos > melhor_acerto:
            melhor_acerto = acertos
            melhor_parametro = params_atuais
            melhor_log_saida = log_temporario
            
    print("\n" + "="*60)
    print(" BUSCA EM GRADE CONCLUÍDA ".center(60, "="))
    print("="*60)
    print("\n🏆 MELHORES PARÂMETROS ENCONTRADOS:")
    for k, v in melhor_parametro.items():
        print(f"   {k} = {v}")
    print(f"\nMelhor taxa de acerto: {melhor_acerto}/{total}\n")
    
    print("Detalhes da melhor execução:")
    print(melhor_log_saida)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("caminho", help="pasta de um clipe, ou pasta 'clips' com varios")
    ap.add_argument("--ver", action="store_true", help="mostra a janela com o rastreamento")
    ap.add_argument("--grid", action="store_true", help="roda a busca em grade de parâmetros")
    a = ap.parse_args()

    if os.path.exists(os.path.join(a.caminho, "frames.csv")) or \
            glob.glob(os.path.join(a.caminho, "*.png")):
        pastas = [a.caminho.rstrip("/\\")]
    else:
        pastas = sorted(d for d in glob.glob(os.path.join(a.caminho, "*")) if os.path.isdir(d))

    if a.grid:
        busca_em_grade(pastas)
    else:
        global GLOBAL_PARAMS
        GLOBAL_PARAMS = {} 
        print(f"{'clipe':<24}{'esperado E/S':>14}{'contado E/S':>14}  resultado")
        acertos = total = 0
        for p in pastas:
            r = processar(p, a.ver)
            if r is None:
                continue
            ent, sai = r
            ee, es = ler_esperado(p)
            ok = (ent, sai) == (ee, es)
            acertos += ok
            total += 1
            print(f"{os.path.basename(p):<24}{f'{ee}/{es}':>14}{f'{ent}/{sai}':>14}  "
                  f"{'OK' if ok else 'ERRO'}")
        print(f"\nacertos: {acertos}/{total}")


if __name__ == "__main__":
    main()
    
