// Ghidra headless script: decompile functions and list their callers / callees.
// Usage (from analyzeHeadless ... -process sora_1st.exe -noanalysis -readOnly
//        -scriptPath tools/ghidra_scripts -postScript Decompile.java <outFile> <addr> [addr...])
// Addresses are exe RVAs ("exe+30100" or "30100") or full VAs ("140030100").
// Prefix an address with "x:" to also list every instruction that references it (data xrefs).
// @category SoraVR
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.Set;
import java.util.TreeSet;

public class Decompile extends GhidraScript {
    private Address parse(String s) {
        s = s.trim().toLowerCase();
        if (s.startsWith("exe+")) s = s.substring(4);
        if (s.startsWith("0x")) s = s.substring(2);
        long v = Long.parseUnsignedLong(s, 16);
        long base = currentProgram.getImageBase().getOffset();
        if (v < base) v += base;
        return toAddr(v);
    }

    private String rva(Address a) {
        return "exe+" + Long.toHexString(a.getOffset() - currentProgram.getImageBase().getOffset());
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: Decompile.java <outFile> <addr> ..."); return; }
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                String arg = args[i];
                boolean xrefs = arg.startsWith("x:");
                Address a = parse(xrefs ? arg.substring(2) : arg);
                if (xrefs) {
                    out.println("==== references to " + rva(a));
                    for (Reference r : getReferencesTo(a)) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        out.println("  " + rva(r.getFromAddress()) + "  " + r.getReferenceType() + "  in " + (f == null ? "?" : f.getName() + " @" + rva(f.getEntryPoint())));
                    }
                    out.println();
                    continue;
                }
                Function f = getFunctionContaining(a);
                if (f == null) { out.println("==== " + rva(a) + ": no function here\n"); continue; }
                out.println("==== " + f.getName() + " @" + rva(f.getEntryPoint()) + " (asked for " + rva(a) + ")");
                Set<String> callers = new TreeSet<>();
                for (Function c : f.getCallingFunctions(monitor)) callers.add(c.getName() + " @" + rva(c.getEntryPoint()));
                Set<String> callees = new TreeSet<>();
                for (Function c : f.getCalledFunctions(monitor)) callees.add(c.getName() + " @" + rva(c.getEntryPoint()));
                out.println("-- called by: " + callers);
                out.println("-- calls:     " + callees);
                DecompileResults res = dec.decompileFunction(f, 120, monitor);
                if (res != null && res.decompileCompleted()) out.println(res.getDecompiledFunction().getC());
                else out.println("(decompile failed: " + (res == null ? "?" : res.getErrorMessage()) + ")");
                out.println();
            }
        }
        dec.dispose();
        println("written " + args[0]);
    }
}
