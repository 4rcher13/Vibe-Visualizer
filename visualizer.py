import sys
import time
import asyncio
import pyaudio
import numpy as np
# Importamos las librerías de WinRT
from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as SessionManager

# --- CONFIGURACIÓN ---
CHANNELS = 1
RATE = 44100
CHUNK = 1024
NUM_BARS = 10       # Numero de barras en el visualizador
MAX_HEIGHT = 8      # Altura máxima de las barras

# --- FUNCIÓN PARA OBTENER CANCIÓN Y ARTISTA ---
async def get_media_info():
    try:
        manager = await SessionManager.request_async()
        current_session = manager.get_current_session()
        if current_session:
            info = await current_session.try_get_media_properties_async()
            return info.title, info.artist
    except Exception:
        pass
    return "Silencio o Desconocido", "Desconocido"

# --- INICIALIZACIÓN DE AUDIO ---
p = pyaudio.PyAudio()
stream = p.open(format=pyaudio.paInt16, channels=CHANNELS, rate=RATE, input=True, frames_per_buffer=CHUNK)

print("\033[2J\033[H") # Limpia la pantalla
print("🎵 Iniciando visualizador...")
time.sleep(1)

try:
    while True:
        # 1. Capturar Audio y Aplicar FFT
        data = stream.read(CHUNK, exception_on_overflow=False)
        data_int = np.frombuffer(data, dtype=np.int16)
        frecuencias = np.abs(np.fft.fft(data_int))[:CHUNK // 2]
        
        # 2. Agrupar en 10 barras y calcular alturas
        diez_grupos = np.array_split(frecuencias, NUM_BARS)
        
        alturas :list[int] = []
        for grupo in diez_grupos: 
            val = int(np.mean(grupo) / 400) # Ajusta este número si las barras se mueven muy poco o demasiado
            alturas.append(max(0, min(val, MAX_HEIGHT)))

        # 3. Obtener metadatos de Windows
        cancion, artista = asyncio.run(get_media_info())
        info_texto = f"🎵 {cancion} - {artista}".center(NUM_BARS * 4)

        # 4. Renderizar Barras Verticales 
        pantalla_buffer = ""
        for fila in range(MAX_HEIGHT, 0, -1):
            linea = ""
            for altura in alturas:
                if altura >= fila:
                    linea += " █  " 
                else:
                    linea += "    " 
            pantalla_buffer += linea + "\n"
        
        pantalla_buffer += "\n" + info_texto + "\n"

        # 5. Dibujar en la terminal
        sys.stdout.write("\033[H" + pantalla_buffer)
        sys.stdout.flush()

        time.sleep(0.06) # ~60 FPS

except KeyboardInterrupt:
    print("\nVisualizador detenido.")
    stream.stop_stream()
    stream.close()
    p.terminate()