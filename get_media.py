import sys
import asyncio
import os
import mmap
import ctypes
from typing import NamedTuple, cast

# Import Windows Runtime APIs
try:
    from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as SessionManager
except ImportError:
    # If the user environment is not ready, we will print a message and exit
    print("Error: No se encontró 'winrt-Windows.Media.Control'. Ejecuta 'pip install -r requirements.txt'")
    sys.exit(1)

SHM_NAME = "VibeVisualizerMetadata"
SHM_SIZE = 512


class MediaProperties(NamedTuple):
    title: str
    artist: str

async def get_media_info():
    try:
        manager = await SessionManager.request_async()
        current_session = manager.get_current_session()
        if current_session:
            info = await current_session.try_get_media_properties_async()
            if info:
                media_info = cast(MediaProperties, info)
                return media_info.title, media_info.artist
    except Exception as e:
        print(f"Error al obtener información de medios: {e}")

def check_parent_alive(parent_pid: int) -> bool:
    # Check if process is still active on Windows
    try:
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, parent_pid)
        if handle:
            exit_code = ctypes.c_ulong()
            kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code))
            kernel32.CloseHandle(handle)
            return exit_code.value == 259  # 259 is STILL_ACTIVE
        return False
    except Exception:
        # Fallback to simple process existence check
        return True

async def main():
    parent_pid: int = os.getppid()
    
    # Create or open named shared memory on Windows
    # tagname specifies the name of the mapping object
    try:
        shm = mmap.mmap(-1, SHM_SIZE, tagname=SHM_NAME, access=mmap.ACCESS_WRITE)
    except Exception as e:
        print(f"Error al crear memoria compartida: {e}")
        return
        
    try:
        while True:
            # Check if parent C program is still alive
            if not check_parent_alive(parent_pid):
                break
                
            title, artist = await get_media_info()
            
            if title != last_title or artist != last_artist:
                last_title = title
                last_artist = artist
                
                # Format: Title\nArtist\0
                data = f"{title}\n{artist}\0".encode('utf-8')
                
                # Truncate if larger than shared memory size
                if len(data) > SHM_SIZE:
                    data = data[:SHM_SIZE-1] + b'\0'
                
                # Write to shared memory
                shm.seek(0)
                shm.write(data)
                # Pad remaining bytes with zeroes
                shm.write(b'\0' * (SHM_SIZE - len(data)))
                
            await asyncio.sleep(0.5)
            
    except KeyboardInterrupt:
        pass
    finally:
        shm.close()

if __name__ == "__main__":
    asyncio.run(main())
