"""Same-volume JSON publication for Windows PowerShell 5.1.

Retain a unique predecessor instead of passing a null backup path: PowerShell
coerces that argument into an empty path for File.Replace. No remote side effect
at import. Callers own transport/deadline and the fresh source file.
"""
import uuid

def replacement_script(source, target, predecessor=None, *, retry_journal=None):
    assert source != target and source and target
    predecessor = predecessor or target+'.previous-'+uuid.uuid4().hex
    assert predecessor not in (source,target)
    def quote(value):return "'"+value.replace("'","''")+"'"
    journal=("[IO.File]::AppendAllText("+quote(retry_journal)+",($code.ToString()+[Environment]::NewLine));") if retry_journal else ''
    return ("$ErrorActionPreference='Stop';$a="+quote(source)+";$b="+quote(target)+
            ";$previous="+quote(predecessor)+
            ";if([IO.File]::Exists($previous)){throw 'Predecessor already exists'};"
            "$watch=[Diagnostics.Stopwatch]::StartNew();while($true){try{"
            "if([IO.File]::Exists($b)){[IO.File]::Replace($a,$b,$previous)}"
            "else{[IO.File]::Move($a,$b)};break}catch [IO.IOException]{"
            "$code=$_.Exception.HResult -band 65535;"+journal+
            "if(($code -ne 32 -and $code -ne 33) -or $watch.ElapsedMilliseconds -ge 750){throw};"
            "Start-Sleep -Milliseconds 25}}")
