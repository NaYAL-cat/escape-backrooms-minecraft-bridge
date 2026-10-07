package dev.rehan.passthrough.client;
import com.google.gson.*;
import dev.rehan.passthrough.*;
import java.net.InetSocketAddress;
import net.minecraft.client.Minecraft;
import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import org.java_websocket.server.WebSocketServer;

/** One versioned loopback connection. All game mutation is dispatched onto the owning thread. */
public final class HostLink extends WebSocketServer {
    private static HostLink instance;
    private volatile WebSocket peer;
    private volatile String session="";
    private HostLink(int port) { super(new InetSocketAddress("127.0.0.1",port)); setReuseAddr(true); setDaemon(true); setConnectionLostTimeout(5); }
    static void launch() { instance=new HostLink(Integer.getInteger("passthrough.port",25599)); instance.start(); Passthrough.events=HostLink::send; }
    public static String session() { return instance==null?"":instance.session; }
    public static void send(String message) {
        var i=instance; if(i==null || i.peer==null || !i.peer.isOpen() || i.session.isEmpty()) return;
        try { JsonObject m=JsonParser.parseString(message).getAsJsonObject(); m.addProperty("v",2); m.addProperty("s",i.session); i.peer.send(m.toString()); }
        catch(RuntimeException e) { Passthrough.LOG.debug("bridge send: {}",e.toString()); }
    }
    @Override public void onStart() { Passthrough.LOG.info("V2 listening on loopback:{}",getPort()); }
    @Override public synchronized void onOpen(WebSocket c,ClientHandshake h) {
        if(peer!=null && peer.isOpen()) { c.close(1008,"Only one bridge is allowed"); return; }
        peer=c; session=""; c.send("{\"t\":\"hello\",\"v\":2}");
    }
    @Override public void onClose(WebSocket c,int code,String reason,boolean remote) {
        if(c!=peer) return; peer=null; session="";
        Minecraft.getInstance().execute(()-> { HostState.reset(); ClientInput.release(Minecraft.getInstance()); });
    }
    @Override public void onError(WebSocket c,Exception e) { Passthrough.LOG.warn("bridge transport: {}",e.toString()); }
    @Override public void onMessage(WebSocket c,String text) {
        if(c!=peer || text.length()>1_048_576) return;
        try {
            JsonObject m=JsonParser.parseString(text).getAsJsonObject();
            if(m.get("v").getAsInt()!=2) { c.close(1008,"V2 required"); return; }
            String type=m.get("t").getAsString(),s=m.get("s").getAsString();
            if(type.equals("hello")) {
                if(s.length()>64 || s.isEmpty()) return;
                if(!s.equals(session)) { session=s; Minecraft.getInstance().execute(()->{HostState.reset(); ClientInput.release(Minecraft.getInstance());}); }
                return;
            }
            if(!s.equals(session)) return;
            Minecraft mc=Minecraft.getInstance();
            mc.execute(()-> {
                if(!s.equals(session)) return;
                try {
                    switch(type) {
                    case "heartbeat" -> { HostState.heartbeat(m.get("active").getAsBoolean(),m.get("render").getAsBoolean());
                        ClientInput.mouseScale=Math.clamp(m.get("mouseScale").getAsDouble(),.05,10); }
                    case "start" -> BridgeWorld.align(m,()->mc.execute(()-> { if(s.equals(session)) { HostState.aligned(true); send("{\"t\":\"aligned\"}"); } }));
                    case "collision" -> BridgeWorld.collision(m.getAsJsonArray("boxes"));
                    case "entities" -> HostEntities.receive(m.getAsJsonArray("entities"));
                    default -> ClientInput.handle(mc,m);
                    }
                } catch(RuntimeException e) { Passthrough.LOG.warn("bridge message {}: {}",type,e.toString()); }
            });
        } catch(RuntimeException e) { Passthrough.LOG.warn("invalid bridge message: {}",e.toString()); }
    }
}
