// Ghidra post-script: export complete static offline RE index to JSON tables (CV-01).
//
// Extracts:
//   - Functions (entry, name, size, is_thunk, calling_conv, return_type, param_count, byte_hash)
//   - Basic Blocks (block_va, end_va, size, instruction_count, func_va, flow_type, is_entry, is_exit)
//   - CFG Flows (src_va, dst_va, func_va, flow_type)
//   - Call Edges (caller_va, callee_va, call_site_va, call_type)
//   - Data Xrefs (func_va, site_va, data_va, access_type: READ/WRITE/OFFSET)
//   - String Xrefs (func_va, string_name, string_va, value)
//   - Switch Cases (func_va, switch_va, case_val, target_va)
//   - Manifest / Metadata (hashes, counts, versions)
//
// Run via:
//   ghidra-analyzeHeadless <proj_dir> <proj_name> -process <binary> -noanalysis \
//     -scriptPath <script_dir> -postScript ExportGhidraIndex.java <out_dir>
// @category OpenRecet

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.security.MessageDigest;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.block.BasicBlockModel;
import ghidra.program.model.block.CodeBlock;
import ghidra.program.model.block.CodeBlockIterator;
import ghidra.program.model.block.CodeBlockReference;
import ghidra.program.model.block.CodeBlockReferenceIterator;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.data.StringDataType;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import ghidra.util.task.ConsoleTaskMonitor;

public class ExportGhidraIndex extends GhidraScript {

    private static String hex(long val) {
        return "0x" + Long.toHexString(val);
    }

    private static String sha256Hex(byte[] data) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] hash = md.digest(data);
            StringBuilder sb = new StringBuilder(hash.length * 2);
            for (byte b : hash) {
                sb.append(String.format("%02x", b & 0xff));
            }
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    private static String jsonEscape(String s) {
        if (s == null) return "";
        StringBuilder sb = new StringBuilder(s.length() + 16);
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\b': sb.append("\\b"); break;
                case '\f': sb.append("\\f"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20 || c > 0x7e) {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
                    break;
            }
        }
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new RuntimeException("ExportGhidraIndex: missing output dir argument");
        }
        File outDir = new File(args[0]);
        if (!outDir.exists()) {
            outDir.mkdirs();
        }

        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        Memory memory = currentProgram.getMemory();
        BasicBlockModel blockModel = new BasicBlockModel(currentProgram);

        int totalFunctions = fm.getFunctionCount();
        println("[ExportGhidraIndex] Indexing " + totalFunctions + " functions from " + currentProgram.getName());

        File funcFile = new File(outDir, "functions.json");
        File blockFile = new File(outDir, "blocks.json");
        File flowFile = new File(outDir, "flows.json");
        File callFile = new File(outDir, "calls.json");
        File dataXrefFile = new File(outDir, "data_xrefs.json");
        File strXrefFile = new File(outDir, "string_xrefs.json");
        File switchFile = new File(outDir, "switch_cases.json");
        File manifestFile = new File(outDir, "manifest.json");

        long totalBlocks = 0;
        long totalFlows = 0;
        long totalCalls = 0;
        long totalDataXrefs = 0;
        long totalStrXrefs = 0;
        long totalSwitchCases = 0;

        // Collect executable SHA-256
        String exeSha256 = currentProgram.getExecutableSHA256();
        if (exeSha256 == null) exeSha256 = "unknown";

        try (
            PrintWriter pwFunc = new PrintWriter(new FileWriter(funcFile));
            PrintWriter pwBlock = new PrintWriter(new FileWriter(blockFile));
            PrintWriter pwFlow = new PrintWriter(new FileWriter(flowFile));
            PrintWriter pwCall = new PrintWriter(new FileWriter(callFile));
            PrintWriter pwData = new PrintWriter(new FileWriter(dataXrefFile));
            PrintWriter pwStr = new PrintWriter(new FileWriter(strXrefFile));
            PrintWriter pwSwitch = new PrintWriter(new FileWriter(switchFile))
        ) {
            pwFunc.println("[");
            pwBlock.println("[");
            pwFlow.println("[");
            pwCall.println("[");
            pwData.println("[");
            pwStr.println("[");
            pwSwitch.println("[");

            boolean firstFunc = true;
            boolean firstBlock = true;
            boolean firstFlow = true;
            boolean firstCall = true;
            boolean firstData = true;
            boolean firstStr = true;
            boolean firstSwitch = true;

            FunctionIterator it = fm.getFunctions(true);
            int funcIdx = 0;

            Set<String> seenCalls = new HashSet<>();
            Set<String> seenDataXrefs = new HashSet<>();
            Set<String> seenStrXrefs = new HashSet<>();
            Set<String> seenFlows = new HashSet<>();

            while (it.hasNext()) {
                if (monitor.isCancelled()) break;
                Function func = it.next();
                funcIdx++;
                if (funcIdx % 200 == 0 || funcIdx == totalFunctions) {
                    println("[ExportGhidraIndex] Processing function " + funcIdx + "/" + totalFunctions +
                            " (blocks: " + totalBlocks + ", flows: " + totalFlows + ")");
                }

                Address entryAddr = func.getEntryPoint();
                long entryVa = entryAddr.getOffset();
                String entryHex = hex(entryVa);
                String funcName = func.getName();
                long bodySize = func.getBody().getNumAddresses();
                boolean isThunk = func.isThunk();
                String callingConv = func.getCallingConventionName();
                if (callingConv == null) callingConv = "unknown";

                DataType retType = func.getReturnType();
                String retTypeName = (retType != null) ? retType.getName() : "void";
                int paramCount = func.getParameterCount();

                // Compute byte hash
                byte[] funcBytes = new byte[(int) Math.min(bodySize, 65536)];
                try {
                    memory.getBytes(entryAddr, funcBytes);
                } catch (Exception e) {
                    Arrays.fill(funcBytes, (byte) 0);
                }
                String byteHash = sha256Hex(funcBytes);

                // Write Function JSON
                if (!firstFunc) pwFunc.println(",");
                firstFunc = false;
                pwFunc.print(String.format(
                    "  {\"va\":\"%s\",\"name\":\"%s\",\"size\":%d,\"is_thunk\":%b,\"calling_conv\":\"%s\",\"return_type\":\"%s\",\"param_count\":%d,\"byte_hash\":\"%s\"}",
                    entryHex, jsonEscape(funcName), bodySize, isThunk, jsonEscape(callingConv),
                    jsonEscape(retTypeName), paramCount, byteHash
                ));

                // Extract Basic Blocks & CFG Flows using BasicBlockModel
                AddressSetView bodySet = func.getBody();
                CodeBlockIterator blockIt = blockModel.getCodeBlocksContaining(bodySet, monitor);
                while (blockIt.hasNext()) {
                    CodeBlock block = blockIt.next();
                    Address bStart = block.getFirstStartAddress();
                    Address bEnd = block.getMaxAddress();
                    if (bStart == null) continue;

                    long bStartVa = bStart.getOffset();
                    long bEndVa = (bEnd != null) ? bEnd.getOffset() : bStartVa;
                    long bSize = (bEndVa >= bStartVa) ? (bEndVa - bStartVa + 1) : 1;
                    FlowType bFlow = block.getFlowType();
                    String flowName = (bFlow != null) ? bFlow.getName() : "UNKNOWN";

                    // Count instructions in block
                    int instCount = 0;
                    InstructionIterator bInsts = listing.getInstructions(new AddressSet(bStart, bEnd), true);
                    while (bInsts.hasNext()) {
                        bInsts.next();
                        instCount++;
                    }

                    boolean isEntry = (bStartVa == entryVa);
                    boolean isExit = (bFlow != null && bFlow.isTerminal());

                    totalBlocks++;
                    if (!firstBlock) pwBlock.println(",");
                    firstBlock = false;
                    pwBlock.print(String.format(
                        "  {\"block_va\":\"%s\",\"end_va\":\"%s\",\"size\":%d,\"instruction_count\":%d,\"func_va\":\"%s\",\"flow_type\":\"%s\",\"is_entry\":%b,\"is_exit\":%b}",
                        hex(bStartVa), hex(bEndVa), bSize, instCount, entryHex, jsonEscape(flowName), isEntry, isExit
                    ));

                    // Block Destinations (Flow Edges)
                    CodeBlockReferenceIterator dstIt = block.getDestinations(monitor);
                    while (dstIt.hasNext()) {
                        CodeBlockReference dstRef = dstIt.next();
                        Address dstAddr = dstRef.getDestinationAddress();
                        if (dstAddr == null) continue;
                        long dstVa = dstAddr.getOffset();
                        FlowType refFlow = dstRef.getFlowType();
                        String refFlowName = (refFlow != null) ? refFlow.getName() : flowName;

                        String flowKey = bStartVa + "->" + dstVa;
                        if (!seenFlows.contains(flowKey)) {
                            seenFlows.add(flowKey);
                            totalFlows++;
                            if (!firstFlow) pwFlow.println(",");
                            firstFlow = false;
                            pwFlow.print(String.format(
                                "  {\"src_va\":\"%s\",\"dst_va\":\"%s\",\"func_va\":\"%s\",\"flow_type\":\"%s\"}",
                                hex(bStartVa), hex(dstVa), entryHex, jsonEscape(refFlowName)
                            ));
                        }
                    }
                }

                // Extract Instructions: Calls, Data Xrefs, String Xrefs, Switch Cases
                InstructionIterator instIt = listing.getInstructions(bodySet, true);
                while (instIt.hasNext()) {
                    Instruction inst = instIt.next();
                    Address instAddr = inst.getAddress();
                    long instVa = instAddr.getOffset();
                    FlowType flow = inst.getFlowType();

                    // 1. Direct and Indirect Calls
                    if (flow.isCall()) {
                        Address[] flowAddrs = inst.getFlows();
                        if (flowAddrs != null && flowAddrs.length > 0) {
                            for (Address fa : flowAddrs) {
                                long targetVa = fa.getOffset();
                                String callKey = entryVa + ":" + instVa + "->" + targetVa;
                                if (!seenCalls.contains(callKey)) {
                                    seenCalls.add(callKey);
                                    totalCalls++;
                                    if (!firstCall) pwCall.println(",");
                                    firstCall = false;
                                    pwCall.print(String.format(
                                        "  {\"caller_va\":\"%s\",\"callee_va\":\"%s\",\"call_site_va\":\"%s\",\"call_type\":\"DIRECT\"}",
                                        entryHex, hex(targetVa), hex(instVa)
                                    ));
                                }
                            }
                        } else {
                            // Indirect call
                            totalCalls++;
                            if (!firstCall) pwCall.println(",");
                            firstCall = false;
                            pwCall.print(String.format(
                                "  {\"caller_va\":\"%s\",\"callee_va\":\"0x0\",\"call_site_va\":\"%s\",\"call_type\":\"INDIRECT\"}",
                                entryHex, hex(instVa)
                            ));
                        }
                    }

                    // 2. Switch Tables / Computed Jumps
                    if (flow.isComputed() && flow.isJump()) {
                        Reference[] refs = inst.getReferencesFrom();
                        int caseIndex = 0;
                        for (Reference r : refs) {
                            if (r.getReferenceType().isData() || r.getReferenceType().isFlow()) {
                                Address toAddr = r.getToAddress();
                                long toVa = toAddr.getOffset();
                                if (toVa >= 0x00401000 && toVa <= 0x00600000) {
                                    totalSwitchCases++;
                                    if (!firstSwitch) pwSwitch.println(",");
                                    firstSwitch = false;
                                    pwSwitch.print(String.format(
                                        "  {\"func_va\":\"%s\",\"switch_va\":\"%s\",\"case_val\":%d,\"target_va\":\"%s\"}",
                                        entryHex, hex(instVa), caseIndex++, hex(toVa)
                                    ));
                                }
                            }
                        }
                    }

                    // 3. Memory & Global Data Xrefs
                    Reference[] instRefs = inst.getReferencesFrom();
                    for (Reference ref : instRefs) {
                        RefType refType = ref.getReferenceType();
                        Address toAddr = ref.getToAddress();
                        if (toAddr == null || !memory.contains(toAddr)) continue;

                        long toVa = toAddr.getOffset();
                        // Ignore code-to-code local branches
                        if (refType.isFlow() && !flow.isCall()) continue;

                        String accessType = "READ";
                        if (refType.isWrite()) accessType = "WRITE";
                        else if (refType.isRead()) accessType = "READ";
                        else if (refType.isData()) accessType = "DATA";

                        // Data xref
                        String xrefKey = entryVa + ":" + instVa + "->" + toVa + ":" + accessType;
                        if (!seenDataXrefs.contains(xrefKey)) {
                            seenDataXrefs.add(xrefKey);
                            totalDataXrefs++;
                            if (!firstData) pwData.println(",");
                            firstData = false;
                            pwData.print(String.format(
                                "  {\"func_va\":\"%s\",\"site_va\":\"%s\",\"data_va\":\"%s\",\"access_type\":\"%s\"}",
                                entryHex, hex(instVa), hex(toVa), accessType
                            ));
                        }

                        // String reference check
                        Data data = listing.getDataAt(toAddr);
                        if (data != null && (data.hasStringValue() || data.getDataType() instanceof StringDataType)) {
                            String strVal = data.getDefaultValueRepresentation();
                            if (strVal != null && strVal.length() > 0) {
                                String strKey = entryVa + ":" + toVa;
                                if (!seenStrXrefs.contains(strKey)) {
                                    seenStrXrefs.add(strKey);
                                    totalStrXrefs++;
                                    if (!firstStr) pwStr.println(",");
                                    firstStr = false;
                                    String strName = "s_" + hex(toVa).substring(2);
                                    pwStr.print(String.format(
                                        "  {\"func_va\":\"%s\",\"string_name\":\"%s\",\"string_va\":\"%s\",\"value\":\"%s\"}",
                                        entryHex, strName, hex(toVa), jsonEscape(strVal)
                                    ));
                                }
                            }
                        }
                    }
                }
            }

            pwFunc.println("\n]");
            pwBlock.println("\n]");
            pwFlow.println("\n]");
            pwCall.println("\n]");
            pwData.println("\n]");
            pwStr.println("\n]");
            pwSwitch.println("\n]");
        }

        // Write Manifest JSON
        try (PrintWriter pwMan = new PrintWriter(new FileWriter(manifestFile))) {
            pwMan.println("{");
            pwMan.println("  \"schema_version\": \"cv01-v1.0\",");
            pwMan.println("  \"program_name\": \"" + jsonEscape(currentProgram.getName()) + "\",");
            pwMan.println("  \"executable_sha256\": \"" + exeSha256 + "\",");
            pwMan.println("  \"functions_count\": " + totalFunctions + ",");
            pwMan.println("  \"blocks_count\": " + totalBlocks + ",");
            pwMan.println("  \"flows_count\": " + totalFlows + ",");
            pwMan.println("  \"calls_count\": " + totalCalls + ",");
            pwMan.println("  \"data_xrefs_count\": " + totalDataXrefs + ",");
            pwMan.println("  \"string_xrefs_count\": " + totalStrXrefs + ",");
            pwMan.println("  \"switch_cases_count\": " + totalSwitchCases + "");
            pwMan.println("}");
        }

        println("[ExportGhidraIndex] Complete! Exported " + totalFunctions + " functions, " +
                totalBlocks + " blocks, " + totalFlows + " flows, " + totalCalls + " calls, " +
                totalDataXrefs + " data xrefs, " + totalStrXrefs + " string xrefs, " +
                totalSwitchCases + " switch cases to " + outDir.getAbsolutePath());
    }
}
