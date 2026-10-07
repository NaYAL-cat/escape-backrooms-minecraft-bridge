package dev.rehan.passthrough.client;

import com.google.gson.JsonObject;
import dev.rehan.passthrough.BridgeWorld;
import net.minecraft.client.*;
import net.minecraft.client.gui.screens.*;
import net.minecraft.client.input.*;
import net.minecraft.world.level.GameType;
import org.lwjgl.sdl.SDLVideo;
import static org.lwjgl.sdl.SDLMouse.*;

/** Physical events from the host window, consumed only on Minecraft's client thread. */
public final class ClientInput {
    private static final RemoteKeyState keys = new RemoteKeyState();
    private static final boolean[] buttons = new boolean[3];
    private static double cx=.5, cy=.5;
    private static Screen lastScreen;
    private static int mods, highSurrogate;
    private static long lastClick;
    private static int lastButton=-1;
    public static double mouseScale=1;
    private ClientInput() {}
    public static boolean remoteCursorVisible() { return HostState.control() && Minecraft.getInstance().gui.screen()!=null; }
    public static int remoteCursorX(int w) { return (int)Math.round(cx*Math.max(1,w-1)); }
    public static int remoteCursorY(int h) { return (int)Math.round(cy*Math.max(1,h-1)); }
    public static double pixelX() { return cx*Minecraft.getInstance().getWindow().getWidth(); }
    public static double pixelY() { return cy*Minecraft.getInstance().getWindow().getHeight(); }
    // The wire protocol uses left/right/middle = 0/1/2. Minecraft 26.3 uses SDL3.
    static MouseButtonInfo mouseButtonInfo(int button, int modifiers) {
        int sdlButton = switch (button) {
            case 0 -> SDL_BUTTON_LEFT;
            case 1 -> SDL_BUTTON_RIGHT;
            case 2 -> SDL_BUTTON_MIDDLE;
            default -> throw new IllegalArgumentException("Unknown bridge mouse button: " + button);
        };
        return new MouseButtonInfo(sdlButton, modifiers);
    }
    private static MouseButtonEvent mouse(Minecraft mc,int b) {
        return new MouseButtonEvent(cx*mc.getWindow().getGuiScaledWidth(),cy*mc.getWindow().getGuiScaledHeight(),mouseButtonInfo(b,mods));
    }
    public static void release(Minecraft mc) {
        keys.release();
        Screen screen=mc.gui.screen();
        for(int b=0;b<3;b++) { if(buttons[b] && screen!=null) screen.mouseReleased(mouse(mc,b)); buttons[b]=false; }
        mods=highSurrogate=0;
    }
    public static void tick(Minecraft mc) {
        Screen s=mc.gui.screen();
        if(s!=lastScreen) { release(mc); lastScreen=s; }
        if(!HostState.control()) release(mc);
    }
    public static void handle(Minecraft mc,JsonObject m) {
        String type=m.get("t").getAsString();
        if(type.equals("release")) { release(mc); return; }
        if(type.equals("view")) {
            int w=Math.clamp(m.get("w").getAsInt(),320,3840),h=Math.clamp(m.get("h").getAsInt(),200,2160);
            mc.getWindow().setWindowed(w,h);
            SDLVideo.SDL_SetWindowSize(mc.getWindow().handle(),w,h);
            return;
        }
        if(!HostState.control() || !type.equals("raw")) return;
        int kind=m.get("kind").getAsInt(),a=m.get("a").getAsInt(),b=m.get("b").getAsInt();
        mods=m.get("mods").getAsInt();
        if(kind==6) { release(mc); return; }
        Screen screen=mc.gui.screen();
        if(kind==1) {
            if(screen!=null) {
                double oldX=cx*mc.getWindow().getGuiScaledWidth(),oldY=cy*mc.getWindow().getGuiScaledHeight();
                cx=Math.clamp(cx+(double)a/mc.getWindow().getWidth(),0,1);
                cy=Math.clamp(cy+(double)b/mc.getWindow().getHeight(),0,1);
                double x=cx*mc.getWindow().getGuiScaledWidth(),y=cy*mc.getWindow().getGuiScaledHeight();
                screen.mouseMoved(x,y);
                for(int button=0;button<3;button++) if(buttons[button]) screen.mouseDragged(mouse(mc,button),x-oldX,y-oldY);
            } else if(mc.player!=null) {
                double s=mc.options.sensitivity().get()*.6+.2;
                double speed=s*s*s*8*mouseScale;
                mc.player.turn(a*speed*(mc.options.invertMouseX().get()?-1:1),b*speed*(mc.options.invertMouseY().get()?-1:1));
            }
        } else if(kind==2) {
            KeyEvent e=keyEvent(a,mods); boolean down=b!=0;
            if(screen!=null) { if(down) screen.keyPressed(e); else screen.keyReleased(e); }
            else if(down && b==1 && a==0x75) BridgeWorld.mode(GameType.SURVIVAL);
            else if(down && b==1 && a==0x76) BridgeWorld.mode(GameType.CREATIVE);
            else if(down && a==0x1b) mc.pauseGame(false);
            else {
                for(KeyMapping k:mc.options.keyMappings) if(k.matches(e)) keys.set(k,down,b==2);
            }
        } else if(kind==3 && a>=0 && a<3) {
            boolean down=b!=0;
            if(screen!=null) {
                if(down && !buttons[a]) {
                    long now=System.currentTimeMillis();
                    screen.mouseClicked(mouse(mc,a),lastButton==a && now-lastClick<250);
                    lastButton=a; lastClick=now;
                } else if(!down && buttons[a]) screen.mouseReleased(mouse(mc,a));
                buttons[a]=down;
            } else {
                MouseButtonEvent e=mouse(mc,a);
                for(KeyMapping k:mc.options.keyMappings) if(k.matchesMouse(e)) keys.set(k,down,false);
            }
        } else if(kind==4) {
            double amount=a/120.0*mc.options.mouseWheelSensitivity().get();
            if(screen!=null) screen.mouseScrolled(cx*mc.getWindow().getGuiScaledWidth(),cy*mc.getWindow().getGuiScaledHeight(),0,amount);
            else if(mc.player!=null) mc.player.getInventory().setSelectedSlot(Math.floorMod(mc.player.getInventory().getSelectedSlot()-(int)Math.signum(amount),9));
        } else if(kind==5 && screen!=null) {
            char ch=(char)a;
            if(Character.isHighSurrogate(ch)) highSurrogate=ch;
            else {
                int code=Character.isLowSurrogate(ch)&&highSurrogate!=0?Character.toCodePoint((char)highSurrogate,ch):a;
                highSurrogate=0;
                if(code>=32) screen.charTyped(new CharacterEvent(code));
            }
        }
    }
    static KeyEvent keyEvent(int vk,int modifiers) {
        int sc=0,k=0;
        if(vk>=65&&vk<=90) { sc=vk-65+4; k=vk+32; }
        else if(vk>=49&&vk<=57) { sc=vk-49+30; k=vk; }
        else if(vk==48) { sc=39; k=48; }
        else if(vk>=112&&vk<=123) sc=vk-112+58;
        else {
            sc=switch(vk) { case 13->40; case 27->41; case 8->42; case 9->43; case 32->44;
                case 189->45; case 187->46; case 219->47; case 221->48; case 220->49; case 186->51; case 222->52;
                case 192->53; case 188->54; case 190->55; case 191->56;
                case 45->73; case 36->74; case 33->75; case 46->76; case 35->77; case 34->78;
                case 39->79; case 37->80; case 40->81; case 38->82;
                case 17,162->224; case 16,160->225; case 18,164->226; case 163->228; case 161->229; case 165->230; default->0; };
            k=switch(vk) { case 13->13; case 27->27; case 8->8; case 9->9; case 32->32;
                case 189->45; case 187->61; case 219->91; case 221->93; case 220->92; case 186->59; case 222->39;
                case 192->96; case 188->44; case 190->46; case 191->47; default->0; };
        }
        if(k==0 && sc!=0) k=sc|0x40000000;
        return new KeyEvent(sc,k,modifiers);
    }
}
