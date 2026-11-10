// Creates a function at every address listed in a file (one hex address per line, '#' comments), for code
// that only data refers to (the BWD2 chunk handlers in the descriptor tables, tools/bwd2_tables.py).
// Usage: ghidra script run tools/ghidra/CreateFunctions.java --project mm --program micro.exe -- /abs/path/addrs.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.nio.file.*;
public class CreateFunctions extends GhidraScript {
  public void run() throws Exception {
    int made = 0, had = 0;
    for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
      line = line.trim();
      if (line.isEmpty() || line.startsWith("#")) continue;
      Address a = toAddr(line.split("\\s+")[0]);   // seg:off
      if (getFunctionAt(a) != null) { had++; continue; }
      disassemble(a);
      if (createFunction(a, null) != null) made++;
    }
    println("created " + made + ", already there " + had);
  }
}
