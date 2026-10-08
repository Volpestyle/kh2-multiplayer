"""Pure command/config construction. Importing this module starts nothing."""
from dataclasses import asdict, dataclass
import ipaddress
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
LOCAL = Path(__file__).resolve().parent / '.local'
TAILNET = ipaddress.ip_network('100.64.0.0/10')


@dataclass(frozen=True)
class Options:
    pid: int
    mode: str
    endpoint: str
    port: int = 7782
    peer_id: str = 'player-1'
    slot: str = 'friend1'
    local_relay: bool = False
    seconds: int = 600
    game_build: str = '1.0.0.10-steam-global'
    content: str = 'none'
    mod: str = 'none'
    transport: str = 'enet'
    steam_self: str = ''
    steam_host: str = ''
    steam_allow: str = ''


def make_plan(options: Options, run_dir: Path, *, runtime: Path, server: Path) -> dict:
    """No IO, DNS, processes, or sockets. Explicit private IPv4 only."""
    if type(options.pid) is not int or not 0 < options.pid < 2**32:
        raise ValueError('Select an existing game PID.')
    if options.mode not in ('host', 'join'):
        raise ValueError('Choose host or join.')
    if options.transport not in ('enet', 'steam'):
        raise ValueError('Choose ENet / relay or Steam (beta).')
    endpoint = ''
    if options.transport == 'enet':
        try:
            address = ipaddress.IPv4Address(options.endpoint.strip())
        except ipaddress.AddressValueError as error:
            raise ValueError('Enter a literal tailnet IPv4 address (100.64.0.0/10).') from error
        if address not in TAILNET:
            raise ValueError('Only an explicit tailnet IPv4 endpoint is supported.')
        endpoint = str(address)
        if type(options.port) is not int or not 1 <= options.port <= 65535:
            raise ValueError('Port must be 1..65535.')
    elif options.local_relay:
        raise ValueError('Steam uses Valve relays. Uncheck the local relay option.')
    if type(options.seconds) is not int or not 30 <= options.seconds <= 3600:
        raise ValueError('Development session limit must be 30..3600 seconds.')
    if not re.fullmatch(r'[A-Za-z0-9_. -]{1,32}', options.peer_id) or options.peer_id != options.peer_id.strip():
        raise ValueError('Use a unique 1..32-character ASCII peer ID: letters, digits, space, _, ., or -.')
    if options.slot not in ('friend1', 'friend2'):
        raise ValueError('Join slot must be friend1 or friend2.')
    if type(options.local_relay) is not bool or (options.local_relay and options.mode != 'host'):
        raise ValueError('Only host mode can start a local relay.')
    for value in (options.game_build, options.content, options.mod):
        if not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', value):
            raise ValueError('Compatibility identifiers must be nonempty ASCII tokens; use none when appropriate.')
    role = 'player' if options.mode == 'host' else options.slot
    config = f'game_build={options.game_build}\ncontent_hash={options.content}\nmod_hash={options.mod}\n'
    runtime_argv = [str(runtime), '--config', str(run_dir / 'runtime.ini'),
                    '--mode', 'campaign_coop', '--network', '--server', endpoint,
                    '--port', str(options.port), '--pid', str(options.pid), '--role', role,
                    '--peer-id', options.peer_id, '--no-camera', '--tick-ms', '16',
                    '--max-ticks', str(math.ceil(options.seconds * 1000 / 16)),
                    '--desync-dir', str(run_dir / 'runtime-desync')]
    if options.transport == 'steam':
        runtime_argv[6:10] = steam_arguments(options)
    relay_argv = None
    if options.local_relay:
        relay_argv = [str(server), '--bind', endpoint, '--port', str(options.port),
                      '--build', options.game_build, '--content', options.content, '--mod', options.mod,
                      '--max-peers', '3', '--desync-dir', str(run_dir / 'relay-desync')]
    return {'options': asdict(options), 'role': role, 'protocol': 10,
            'runtime_config': config, 'runtime_argv': runtime_argv, 'relay_argv': relay_argv,
            'wall_limit_seconds': options.seconds, 'run_dir': str(run_dir)}


def steam_id(text):
    """Same public-universe individual desktop SteamID64 contract as SteamBroker."""
    if not isinstance(text, str) or not re.fullmatch(r'[0-9]{17}', text):
        raise ValueError('Paste a 17-digit SteamID64, not a profile URL or friend code.')
    value = int(text)
    if (value >> 56 != 1 or (value >> 52) & 15 != 1 or
            (value >> 32) & 0xfffff != 1 or value & 0xffffffff == 0):
        raise ValueError('SteamID must identify a public individual desktop account.')
    return text


def steam_arguments(options):
    own = steam_id(options.steam_self)
    if options.mode == 'host':
        ids = [steam_id(item.strip()) for item in options.steam_allow.split(',')]
        if not 1 <= len(ids) <= 2 or len(set(ids)) != len(ids) or own in ids:
            raise ValueError('Allow one or two distinct friends, excluding your own SteamID.')
        return ['--steam-host', *[arg for identity in ids for arg in ('--steam-allow', identity)]]
    from steam_flow import host_identity
    target = host_identity(options.steam_host)
    if target == own:
        raise ValueError('Host SteamID must belong to another account.')
    return ['--steam-join', target]
