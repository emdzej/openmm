// Writes the listing of the program's initialised blocks for reading hand-written assembly: one line per
// code unit (address, bytes, instruction or data), function starts and labels on their own lines, the
// references to each instruction, and the ranges no analysis reached marked "undefined". Into re/ only:
// it is the game's code.
// Usage: ghidra script run tools/ghidra/DumpListing.java --project mm --program micro.exe -- /abs/path/re/micro.lst
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DumpListing extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 1) { printerr("usage: DumpListing.java <out.lst>"); return; }
    Listing l = currentProgram.getListing();
    ReferenceManager rm = currentProgram.getReferenceManager();
    long code = 0, data = 0, undef = 0;
    try (PrintWriter w = new PrintWriter(new FileWriter(args[0]))) {
      for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
        if (!b.isInitialized() || !b.isLoaded()) continue;
        w.println("; ==== block " + b.getName() + " " + b.getStart() + "-" + b.getEnd());
        Address undefStart = null;
        CodeUnitIterator it = l.getCodeUnits(new AddressSet(b.getStart(), b.getEnd()), true);
        while (it.hasNext()) {
          CodeUnit cu = it.next();
          Address a = cu.getAddress();
          boolean isUndef = cu instanceof Data && !((Data) cu).isDefined();
          if (isUndef) { if (undefStart == null) undefStart = a; undef += cu.getLength(); continue; }
          if (undefStart != null) {
            w.println(undefStart + "  ; undefined to " + a.subtract(1));
            undefStart = null;
          }
          Function f = getFunctionAt(a);
          if (f != null) w.println("\n; ---- " + f.getName() + " @ " + a + (f.getComment() != null ? "  " + f.getComment() : ""));
          else {
            Symbol s = getSymbolAt(a);
            if (s != null && s.getSource() != SourceType.DEFAULT || (s != null && rm.getReferenceCountTo(a) > 0))
              w.println(s.getName() + ":");
          }
          StringBuilder refs = new StringBuilder();
          int n = 0;
          for (Reference r : rm.getReferencesTo(a)) {
            if (n++ < 6) refs.append(n > 1 ? "," : "").append(r.getFromAddress());
          }
          if (n > 6) refs.append(",+").append(n - 6);
          StringBuilder hex = new StringBuilder();
          byte[] bytes = cu.getBytes();
          for (int i = 0; i < bytes.length && i < 8; i++) hex.append(String.format("%02x", bytes[i] & 0xff));
          String text = cu instanceof Instruction ? cu.toString() : ((Data) cu).getDataType().getName() + " " + ((Data) cu).getDefaultValueRepresentation();
          if (cu instanceof Instruction) code += cu.getLength(); else data += cu.getLength();
          w.println(String.format("%s  %-16s %s%s", a, hex, text, n > 0 ? "    ; <- " + refs : ""));
        }
        if (undefStart != null) w.println(undefStart + "  ; undefined to " + b.getEnd());
      }
    }
    println("code " + code + " bytes, data " + data + ", undefined " + undef);
  }
}
