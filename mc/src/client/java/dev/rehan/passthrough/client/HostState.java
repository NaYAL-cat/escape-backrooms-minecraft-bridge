package dev.rehan.passthrough.client;
import dev.rehan.passthrough.Passthrough;
import dev.rehan.passthrough.BridgeWorld;
import net.minecraft.client.Minecraft;
import net.minecraft.world.level.Level;

/** Connection state only. Never writes camera angles or coordinates into Minecraft. */
public final class HostState {
    private static volatile long heartbeat;
    private static volatile boolean enabled,render,aligned;
    public static void heartbeat(boolean input,boolean view) { heartbeat=System.nanoTime(); enabled=input; render=view; }
    public static void aligned(boolean value) { aligned=value; }
    public static boolean live() { return aligned && BridgeWorld.ready() && System.nanoTime()-heartbeat<1_000_000_000L; }
    public static boolean control() { return live() && enabled; }
    public static boolean exporting() { return live() && render; }
    public static boolean fullWorld() { var mc=Minecraft.getInstance(); return mc.level!=null && !mc.level.dimension().equals(Level.OVERWORLD); }
    public static void reset() { heartbeat=0; enabled=render=aligned=false; Passthrough.active=false; Passthrough.minecraftDrive=false; }
    public static void beginFrame() { Passthrough.active=exporting()&&!fullWorld(); Passthrough.minecraftDrive=control(); }
}
