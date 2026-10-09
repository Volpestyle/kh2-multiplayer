"""Runtime PC2 consent, active-console and driver session binding."""
import os
from .binding import active_console_session, require_session
from .system_sessions import snapshot


def require_current_session(expected):
    """Recheck at admission and immediately before EVERY game launch; never cache."""
    return require_session(expected, snapshot(), os.getpid(), active_console_session())
