// Lists references into [rva - window, rva + window] for each target RVA and
// decompiles up to maxDecomp referencing functions per target.
//
// Args (positional; analyzeHeadless.bat splits on ',' and '='):
//   window maxDecomp rva [rva ...]       e.g. 0x40 3 0x7435D0
// Run through scripts/ghidra.ps1 -Xrefs.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.util.LinkedHashSet;
import java.util.Set;

public class XrefDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("usage: window maxDecomp rva [rva ...]");
            return;
        }
        long window = Long.decode(args[0]);
        int maxDecomp = Integer.parseInt(args[1]);
        long base = currentProgram.getImageBase().getOffset();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);

        for (int i = 2; i < args.length; i++) {
            long rva = Long.decode(args[i]);
            println(String.format("=== XREFS rva=0x%X window=+-0x%X", rva, window));
            Set<Function> funcs = new LinkedHashSet<>();
            int count = 0;
            for (long off = rva - window; off <= rva + window; off++) {
                for (Reference ref : getReferencesTo(toAddr(base + off))) {
                    Function f = getFunctionContaining(ref.getFromAddress());
                    if (count++ < 200) {
                        println(String.format("  0x%X <- 0x%X %s in %s", off,
                            ref.getFromAddress().getOffset() - base, ref.getReferenceType(),
                            f == null ? "<none>" : String.format("%s@0x%X", f.getName(),
                                f.getEntryPoint().getOffset() - base)));
                    }
                    if (f != null) funcs.add(f);
                }
            }
            println(String.format("  total refs: %d, functions: %d", count, funcs.size()));
            int n = 0;
            for (Function f : funcs) {
                if (n++ >= maxDecomp) break;
                DecompileResults res = ifc.decompileFunction(f, 60, monitor);
                if (!res.decompileCompleted()) continue;
                println(String.format("----- %s@0x%X -----", f.getName(),
                    f.getEntryPoint().getOffset() - base));
                for (String line : res.getDecompiledFunction().getC().split("\\R")) {
                    println(line);
                }
            }
        }
    }
}
