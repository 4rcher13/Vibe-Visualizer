import sys
import asyncio
import os
import mmap
import ctypes
from typing import NamedTuple, Optional

# Import Windows Runtime APIs
try:
    from winrt.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as SessionManager,
        GlobalSystemMediaTransportControlsSession as Session,
    )
except ImportError:
    sys.exit(1)


SHM_NAME = "VibeVisualizerMetadata"
SHM_SIZE = 512
PARENT_CHECK_INTERVAL = 3.0


class MediaProperties(NamedTuple):
    title: str
    artist: str


EMPTY_MEDIA = MediaProperties("", "")


def check_parent_alive(parent_pid: int) -> bool:
    """Comprueba si el proceso C padre sigue activo en Windows."""
    try:
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        kernel32 = ctypes.windll.kernel32

        handle = kernel32.OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            False,
            parent_pid,
        )

        if not handle:
            return False

        try:
            exit_code = ctypes.c_ulong()
            if not kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
                return False

            return exit_code.value == 259  # STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)

    except Exception:
        return True


def write_metadata(shm: mmap.mmap, title: str, artist: str) -> None:
    """Escribe los metadatos actuales en memoria compartida."""
    data = f"{title}\n{artist}\0".encode("utf-8")

    if len(data) >= SHM_SIZE:
        data = data[: SHM_SIZE - 1] + b"\0"

    shm.seek(0)
    shm.write(data)

    remaining = SHM_SIZE - len(data)
    if remaining > 0:
        shm.write(b"\0" * remaining)


class MediaWatcher:
    def __init__(self, shm: mmap.mmap): 
        self.shm = shm
        self.manager: Optional[SessionManager] = None
        self.current_session: Optional[Session] = None
        self._media_token = None
        self._loop = asyncio.get_event_loop()
        self.last_media: Optional[MediaProperties] = None

    async def start(self):
        self.manager = await SessionManager.request_async()
        self.manager.add_current_session_changed(self._on_current_session_changed)
        await self._attach_to_current_session()

    def _on_current_session_changed(self):
        self._loop.call_soon_threadsafe(
            lambda: asyncio.ensure_future(self._attach_to_current_session())
        )

    def _on_media_properties_changed(self, sender, args):
        self._loop.call_soon_threadsafe(
            lambda: asyncio.ensure_future(self._refresh_media())
        )

    async def _attach_to_current_session(self):
        if self.current_session is not None and self._media_token is not None:
            try:
                self.current_session.remove_media_properties_changed(self._media_token)
            except Exception:
                pass
            self._media_token = None

        self.current_session = self.manager.get_current_session()

        if self.current_session is not None:
            self._media_token = self.current_session.add_media_properties_changed(self._on_media_properties_changed)

        await self._refresh_media()

    async def _refresh_media(self):
        media = await self._get_media_info()
        if media != self.last_media:
            self.last_media = media
            write_metadata(self.shm, media.title, media.artist)

    async def _get_media_info(self) -> MediaProperties:
        try:
            if self.current_session is None:
                return EMPTY_MEDIA

            info = await self.current_session.try_get_media_properties_async()

            if info is None:
                return EMPTY_MEDIA

            title = info.title.strip() if info.title else ""
            artist = info.artist.strip() if info.artist else ""

            return MediaProperties(title, artist)

        except Exception:
            return EMPTY_MEDIA


async def main():
    parent_pid = os.getppid()
    shm = None

    try:
        shm = mmap.mmap(
            -1,
            SHM_SIZE,
            tagname=SHM_NAME,
            access=mmap.ACCESS_WRITE,
        )
    except Exception:
        return

    try:
        watcher = MediaWatcher(shm)
        await watcher.start()

        while check_parent_alive(parent_pid):
            await asyncio.sleep(PARENT_CHECK_INTERVAL)

    except KeyboardInterrupt:
        pass
    finally:
        if shm is not None:
            shm.close()


if __name__ == "__main__":
    asyncio.run(main())
