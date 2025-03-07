from flask import Flask, render_template, request, jsonify
import socket
import threading

app = Flask(__name__)

# Variables globales para la conexión y respuestas
sock = None
respuestas = []
conectado = False

def escuchar_respuestas():
    """Escucha las respuestas del ESP01 y las guarda en la lista 'respuestas'."""
    global sock, respuestas
    while True:
        try:
            respuesta = sock.recv(1024)
            if respuesta:
                respuestas.append(respuesta.decode("utf-8"))
            else:
                break
        except Exception:
            break

def conectar_esp():
    """Establece la conexión TCP con el ESP01."""
    global sock, conectado
    ESP_IP = "192.168.137.18"
    ESP_PUERTO = 23
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((ESP_IP, ESP_PUERTO))
        conectado = True
        threading.Thread(target=escuchar_respuestas, daemon=True).start()
    except Exception:
        conectado = False

@app.route("/", methods=["GET", "POST"])
def index():
    return render_template("index.html", respuestas=respuestas, conectado=conectado)

@app.route("/send_comando", methods=["POST"])
def send_comando():
    """Envía un comando sin recargar la página."""
    global sock
    comando = request.json.get("comando")
    if sock and comando:
        try:
            sock.sendall(comando.encode("utf-8"))
            return jsonify({"success": True})
        except Exception:
            return jsonify({"success": False}), 500
    return jsonify({"success": False}), 400

@app.route("/get_respuestas")
def get_respuestas():
    return jsonify({"respuestas": respuestas})

if __name__ == "__main__":
    conectar_esp()
    app.run(host="0.0.0.0", port=5000, debug=True)
