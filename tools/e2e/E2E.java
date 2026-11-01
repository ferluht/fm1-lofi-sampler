import com.ferluht.lofi.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.TimeUnit;

/** Runs the real Java Uploader against the C device process (core/dev_server) over a lossy link. */
public class E2E {
    static Process proc; static BufferedReader out; static PrintWriter in; static Random rnd;
    static String ask(String line) throws IOException { in.println(line); in.flush(); return out.readLine(); }
    static String hex(byte[] b) { StringBuilder s = new StringBuilder(); for (byte x : b) s.append(String.format("%02x", x)); return s.toString(); }
    static byte[] unhex(String s) { byte[] b = new byte[s.length() / 2]; for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(s.substring(2 * i, 2 * i + 2), 16); return b; }
    public static void main(String[] a) throws Exception {
        byte[] img = Files.readAllBytes(Paths.get(a[0])); int loss = Integer.parseInt(a[1]); rnd = new Random(a.length > 3 ? Long.parseLong(a[3]) : 7);
        proc = new ProcessBuilder(a[2]).redirectErrorStream(false).start(); out = new BufferedReader(new InputStreamReader(proc.getInputStream())); in = new PrintWriter(proc.getOutputStream());
        final LinkedList<byte[]> replies = new LinkedList<>();
        Uploader.Link link = new Uploader.Link() {
            public void send(byte[] m) { try { if (rnd.nextInt(100) < loss) return; String r = ask(hex(m)); if (!r.isEmpty() && rnd.nextInt(100) >= loss) replies.add(unhex(r)); } catch (IOException e) { throw new RuntimeException(e); } }
            public byte[] receive(long t) { return replies.isEmpty() ? null : replies.removeFirst(); }
            public void drain() { replies.clear(); }
        };
        Uploader up = new Uploader(link); up.pacingMs = 0; up.replyTimeoutMs = 5;
        long t0 = System.currentTimeMillis();
        up.send(img, (s, t, st) -> {});
        long crc = LofiProto.crc32(img, 0, img.length);
        String d = ask("dump"); String want = String.format("state=1 size=%d crc=%08x", img.length, crc);
        System.out.println("loss " + loss + "%: " + d + (d.equals(want) ? "  OK" : "  MISMATCH, wanted " + want) + "  blocks " + up.blocksSent + " resent " + up.blocksResent + " retries " + up.retries + " in " + (System.currentTimeMillis() - t0) + " ms");
        ask("reboot"); String d2 = ask("dump"); System.out.println("after reboot: " + d2 + (d2.equals(want) ? "  OK" : "  MISMATCH"));
        proc.destroy(); if (!d.equals(want) || !d2.equals(want)) System.exit(1);
    }
}
