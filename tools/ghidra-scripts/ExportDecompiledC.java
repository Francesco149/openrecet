// Ghidra post-script: dump decompiled C for every function to docs/decompiled/.
//
// Java works in plain headless mode without PyGhidra. Output:
//   docs/decompiled/all.c              — flat file with every function
//   docs/decompiled/by-address/<addr>.c — one file per function (by entry)
//   docs/decompiled/by-name/<name>.c    — symbolic alias when name != FUN_<addr>
//   docs/decompiled/functions.csv       — index: name, addr, size, thunk, cc
//
// Run via:
//   ghidra-analyzeHeadless ... -postScript ExportDecompiledC.java <out_dir>
// @category OpenRecet

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;

public class ExportDecompiledC extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new RuntimeException("ExportDecompiledC: missing output dir argument");
        }
        File out = new File(args[0]);
        File byAddr = new File(out, "by-address");
        File byName = new File(out, "by-name");
        for (File d : new File[]{out, byAddr, byName}) {
            if (!d.isDirectory() && !d.mkdirs()) {
                throw new IOException("could not create " + d);
            }
        }

        DecompInterface decomp = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        decomp.setOptions(opts);
        decomp.openProgram(currentProgram);

        FunctionManager fm = currentProgram.getFunctionManager();
        int total = fm.getFunctionCount();
        println("[ExportDecompiledC] " + total + " functions to decompile");

        File flatFile = new File(out, "all.c");
        File csvFile = new File(out, "functions.csv");

        try (
            PrintWriter flat = new PrintWriter(new FileWriter(flatFile));
            PrintWriter csv = new PrintWriter(new FileWriter(csvFile))
        ) {
            csv.println("name,entry,size,is_thunk,calling_conv");

            ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();
            FunctionIterator it = fm.getFunctions(true);
            int i = 0;
            while (it.hasNext()) {
                if (monitor.isCancelled()) break;
                Function func = it.next();
                i++;
                if (i % 100 == 0) {
                    println("[ExportDecompiledC]   " + i + "/" + total);
                }

                String name = func.getName();
                String entry = "0x" + Long.toHexString(func.getEntryPoint().getOffset());
                long bodySize = func.getBody().getNumAddresses();
                boolean thunk = func.isThunk();
                String cc = func.getCallingConventionName();
                if (cc == null) cc = "";

                csv.print(csvField(name));
                csv.print(",");
                csv.print(entry);
                csv.print(",");
                csv.print(bodySize);
                csv.print(",");
                csv.print(thunk);
                csv.print(",");
                csv.println(csvField(cc));

                String code;
                try {
                    DecompileResults res = decomp.decompileFunction(func, 60, monitor);
                    if (res != null && res.decompileCompleted()) {
                        code = res.getDecompiledFunction().getC();
                    } else {
                        String err = (res == null) ? "null result" : res.getErrorMessage();
                        code = "// decompile failed: " + err + "\n";
                    }
                } catch (Exception e) {
                    code = "// exception during decompile: " + e + "\n";
                }

                String header = "/* ===== " + name + " @ " + entry +
                                " (" + bodySize + " bytes) ===== */\n";
                flat.print(header);
                flat.print(code);
                flat.print("\n");

                // by-address file: hex without "0x"
                String addrFile = entry.substring(2) + ".c";
                try (PrintWriter f = new PrintWriter(new FileWriter(new File(byAddr, addrFile)))) {
                    f.print(header);
                    f.print(code);
                }

                if (!name.startsWith("FUN_") && !name.equals("_start") && !name.equals("entry")) {
                    StringBuilder safe = new StringBuilder();
                    for (char c : name.toCharArray()) {
                        if (Character.isLetterOrDigit(c) || c == '.' || c == '_' || c == '-') {
                            safe.append(c);
                        } else {
                            safe.append('_');
                        }
                    }
                    try (PrintWriter f = new PrintWriter(
                            new FileWriter(new File(byName, safe.toString() + ".c")))) {
                        f.print(header);
                        f.print(code);
                    }
                }
            }
            println("[ExportDecompiledC] done — " + i + " functions written");
        }
    }

    private static String csvField(String s) {
        if (s == null) return "";
        if (s.indexOf(',') < 0 && s.indexOf('"') < 0 && s.indexOf('\n') < 0) return s;
        return "\"" + s.replace("\"", "\"\"") + "\"";
    }
}
