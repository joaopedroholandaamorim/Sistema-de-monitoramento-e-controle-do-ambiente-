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

import cv2
import numpy as np

ESCALA = 0.25         # Reduz para 80x60 para caber na RAM da ESP32
P1 = (0.00, 0.00)     # Mantém a proporção da linha diagonal
P2 = (1.00, 1.00)     
PONTO_PORTA = (0.20, 0.05)   

BANDA = 3             # Zona morta proporcional à baixa resolução
LIMIAR = 25           # Sensibilidade de diferença de luz
ALFA_BG = 0.02        
AREA_MIN = 10         # Blocos menores já contam como pessoa na resolução 80x60
DIST_MAX = 10         # Deslocamento curto em baixa resolução
FALTAS_MAX = 5        
AQUECIMENTO = 15      
LUZ_GLOBAL = 0.40     
AREA_PESSOA = None
# ========================================================================

FONT = cv2.FONT_HERSHEY_SIMPLEX


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

    for i, arq in enumerate(arqs):
        gray = cv2.imread(arq, cv2.IMREAD_GRAYSCALE)
        pequeno = cv2.resize(gray, (w, h), interpolation=cv2.INTER_AREA)
        g = cv2.GaussianBlur(pequeno, (5, 5), 0).astype(np.float32)

        if i < AQUECIMENTO:
            acc.append(g)
            if i == AQUECIMENTO - 1:
                bg = np.mean(acc, axis=0)
        else:
            bruto = (np.abs(g - bg) > LIMIAR).astype(np.uint8)

            if bruto.mean() > LUZ_GLOBAL:
                # mudanca global de luz: reaprende o fundo e ignora este quadro
                bg = g.copy()
                mask = np.zeros_like(bruto)
                for t in tracks:
                    t["faltas"] += 1
            else:
                livre = bruto == 0
                bg[livre] += ALFA_BG * (g[livre] - bg[livre])

                mask = cv2.morphologyEx(bruto, cv2.MORPH_OPEN, kernel)
                mask = cv2.dilate(mask, kernel, iterations=2)
                n, _, stats, cents = cv2.connectedComponentsWithStats(mask, connectivity=8)
                blobs = [(cents[j], stats[j, cv2.CC_STAT_AREA])
                         for j in range(1, n) if stats[j, cv2.CC_STAT_AREA] >= AREA_MIN]

                # associa cada rastro ao blob mais proximo (guloso)
                usados = set()
                for t in tracks:
                    melhor, menor = None, DIST_MAX
                    for bi, (c, a) in enumerate(blobs):
                        if bi in usados:
                            continue
                        d = np.hypot(c[0] - t["pos"][0], c[1] - t["pos"][1])
                        if d < menor:
                            melhor, menor = bi, d
                    if melhor is None:
                        t["faltas"] += 1
                        continue
                    usados.add(melhor)
                    c, a = blobs[melhor]
                    t["pos"] = (float(c[0]), float(c[1]))
                    t["area"] = 0.7 * t["area"] + 0.3 * float(a)
                    t["faltas"] = 0
                    novo = lado(t["pos"], t["lado"])
                    if t["lado"] != 0 and novo != 0 and novo != t["lado"]:
                        qtd = max(1, round(t["area"] / AREA_PESSOA)) if AREA_PESSOA else 1
                        if novo == 1:      # porta -> sala
                            ent += qtd
                        else:              # sala -> porta
                            sai += qtd
                    t["lado"] = novo

                # blobs sem rastro viram rastros novos
                for bi, (c, a) in enumerate(blobs):
                    if bi not in usados:
                        pos = (float(c[0]), float(c[1]))
                        tracks.append({"id": prox_id, "pos": pos, "area": float(a),
                                       "faltas": 0, "lado": lado(pos, 0)})
                        prox_id += 1

            tracks = [t for t in tracks if t["faltas"] <= FALTAS_MAX]

        if ver:
            vis = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
            cv2.line(vis, tuple((p1 * esc).astype(int)), tuple((p2 * esc).astype(int)),
                     (0, 255, 255), 1)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("caminho", help="pasta de um clipe, ou pasta 'clips' com varios")
    ap.add_argument("--ver", action="store_true", help="mostra a janela com o rastreamento")
    a = ap.parse_args()

    if os.path.exists(os.path.join(a.caminho, "frames.csv")) or \
            glob.glob(os.path.join(a.caminho, "*.png")):
        pastas = [a.caminho.rstrip("/\\")]
    else:
        pastas = sorted(d for d in glob.glob(os.path.join(a.caminho, "*")) if os.path.isdir(d))

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