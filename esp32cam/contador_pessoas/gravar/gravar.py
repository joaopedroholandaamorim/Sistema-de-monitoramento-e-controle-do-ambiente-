import cv2, csv, os, sys, time

# Uso: python gravar.py <nome_do_clipe> [url_do_stream]
URL = sys.argv[2] if len(sys.argv) > 2 else "http://10.42.0.77:81/stream"
nome = sys.argv[1] if len(sys.argv) > 1 else time.strftime("clip_%H%M%S")
pasta = os.path.join("clips", nome)
os.makedirs(pasta, exist_ok=True)

cap = cv2.VideoCapture(URL)
if not cap.isOpened():
    sys.exit("Nao abriu o stream")

f_fr = open(os.path.join(pasta, "frames.csv"), "w", newline="")
f_ev = open(os.path.join(pasta, "gabarito.csv"), "w", newline="")
w_fr = csv.writer(f_fr); w_fr.writerow(["idx", "t"])
w_ev = csv.writer(f_ev); w_ev.writerow(["idx", "t", "evento"])

ent = sai = idx = 0
t0 = time.time()
while True:
    ok, frame = cap.read()
    if not ok:
        break
    t = time.time() - t0
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    cv2.imwrite(os.path.join(pasta, f"{idx:05d}.png"), gray)   # PNG limpo, sem linhas
    w_fr.writerow([idx, f"{t:.3f}"])

    vis = frame.copy()
    h, w = vis.shape[:2]
    
    # --- DEFINIÇÃO DAS LINHAS ---
    #y_porta = int(h * 0.40)
    #y_conf  = int(h * 0.60)
    
    # Pontos da linha diagonal (ex: de x=20% no topo até x=80% na base)
    pt_superior = (int(w * 0.0), 0)
    pt_inferior = (int(w * 1.0), h)

    # --- DESENHO DAS LINHAS NO VISUALIZADOR ---
    #cv2.line(vis, (0, y_porta), (w, y_porta), (0, 255, 0), 1)          # verde: limite da porta
    #cv2.line(vis, (0, y_conf),  (w, y_conf),  (0, 0, 255), 1)          # vermelha: confirmação inferior
   # cv2.line(vis, (0, int(h * 0.50)), (w, int(h * 0.50)), (255, 255, 255), 1)   # meio (soleira)
    cv2.line(vis, pt_superior, pt_inferior, (255, 255, 0), 2)          # ciano: diagonal

    # --- TEXTOS NA TELA ---
    #cv2.putText(vis, "porta", (5, y_porta - 4), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)
    #cv2.putText(vis, "diagonal", (pt_superior[0] + 5, 15), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 255, 0), 1)
    cv2.putText(vis, f"E:{ent} S:{sai}  [e]=entrou [s]=saiu [q]=sair",
                (5, 15), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 0), 1)
                
    cv2.imshow("gravando", vis)

    k = cv2.waitKey(1) & 0xFF
    if k == ord('e'):
        ent += 1; w_ev.writerow([idx, f"{t:.3f}", "entrada"])
    elif k == ord('s'):
        sai += 1; w_ev.writerow([idx, f"{t:.3f}", "saida"])
    elif k == ord('q'):
        break
    idx += 1

f_fr.close(); f_ev.close(); cap.release(); cv2.destroyAllWindows()
print(f"{nome}: {idx} quadros | entradas={ent} saidas={sai}")