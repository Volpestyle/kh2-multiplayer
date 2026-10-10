"""One BOM accepted on input; none emitted. No locale-default decoding."""
import base64,json,os
from pathlib import Path

def checked_text(data):
    if isinstance(data,bytes):
        data=data.decode('utf-8',errors='strict')
    if not isinstance(data,str):raise TypeError('UTF-8 text or bytes required')
    if data.startswith('\ufeff'):data=data[1:]
    if data.startswith(('\ufeff','ï»¿','‹¯¨')):
        raise ValueError('multiple BOM or locale-decoded BOM refused')
    return data

def read_text(path):return checked_text(Path(path).read_bytes())
def read_json(path):return json.loads(read_text(path))
def write_text(path,value,*,exclusive=True):
    data=checked_text(value).encode('utf-8')
    with Path(path).open('xb' if exclusive else 'wb') as f:f.write(data)
def write_json(path,value,*,exclusive=True):
    write_text(path,json.dumps(value,ensure_ascii=False,indent=2)+'\n',exclusive=exclusive)
def encoded_command(script):return base64.b64encode(checked_text(script).encode('utf-16le')).decode('ascii')
def utf8_env(extra=None):
    return {**os.environ,**(extra or {}),'PYTHONUTF8':'1','PYTHONUNBUFFERED':'1'}


def powershell_argv(*arguments):
    """Process-scoped policy only; never changes machine/user policy."""
    return ["powershell.exe", "-NoProfile", "-NonInteractive",
            "-ExecutionPolicy", "Bypass", *map(str, arguments)]
