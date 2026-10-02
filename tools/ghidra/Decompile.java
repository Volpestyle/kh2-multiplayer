// Decompiles the function containing each RVA and lists its callers and
// callees, so an agent can walk a call chain without the Ghidra GUI.
//
// Args (positional): rva [rva ...]          e.g. 0x3C86A0 0x3BFD30
// Run through scripts/ghidra.ps1 -Decompile.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class Decompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);

        for (String arg : getScriptArgs()) {
            long rva = Long.decode(arg);
            Function f = getFunctionContaining(toAddr(base + rva));
            if (f == null) {
                println(String.format("=== 0x%X: no function contains this address", rva));
                continue;
            }
            long entry = f.getEntryPoint().getOffset() - base;
            println(String.format("=== %s@0x%X (contains 0x%X, %d bytes)", f.getName(), entry,
                rva, f.getBody().getNumAddresses()));
            StringBuilder callers = new StringBuilder();
            for (Function c : f.getCallingFunctions(monitor)) {
                callers.append(String.format(" %s@0x%X", c.getName(),
                    c.getEntryPoint().getOffset() - base));
            }
            StringBuilder callees = new StringBuilder();
            for (Function c : f.getCalledFunctions(monitor)) {
                callees.append(String.format(" %s@0x%X", c.getName(),
                    c.getEntryPoint().getOffset() - base));
            }
            println("  callers:" + (callers.length() == 0 ? " <none>" : callers));
            println("  callees:" + (callees.length() == 0 ? " <none>" : callees));
            DecompileResults res = ifc.decompileFunction(f, 120, monitor);
            if (!res.decompileCompleted()) {
                println("  decompile failed: " + res.getErrorMessage());
                continue;
            }
            // One println per line: the wrapper keeps only tagged script lines.
            for (String line : res.getDecompiledFunction().getC().split("\\R")) {
                println(line);
            }
        }
    }
}
