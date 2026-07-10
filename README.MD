# 🎵 Vibe-Visualizer

Un visualizador de audio minimalista y ligero para la terminal de Windows con la estética limpia. `Vibe-Visualizer` captura el audio del sistema en tiempo real, procesa las frecuencias mediante la Transformada Rápida de Fourier (FFT) y muestra las barras animadas junto con los metadatos de la canción que está sonando.

## Características

* **10 Barras Verticales:** Animación fluida generada con bloques Unicode de forma eficiente.
* **Detección Automática:** Muestra el título de la canción y el artista centrado abajo en tiempo real.
* **Alto Rendimiento:** Renderizado directo en la terminal mediante códigos de escape ANSI, evitando parpadeos y uso excesivo de CPU.
* **Compatibilidad Nativa:** Diseñado para correr directamente en Windows Terminal sin necesidad de WSL ni compiladores pesados.

## Requisitos e Instalación

Asegúrate de tener un entorno virtual de Python activado y ejecuta el siguiente comando para instalar las dependencias necesarias:

```bash
pip install -r requirements.txt
