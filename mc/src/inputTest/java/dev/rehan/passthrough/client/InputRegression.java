package dev.rehan.passthrough.client;

import com.mojang.blaze3d.platform.InputConstants;
import dev.rehan.passthrough.client.mixin.KeyMappingAccessor;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.ToggleKeyMapping;
import net.minecraft.client.input.MouseButtonEvent;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.tree.*;

/** Headless tests against the installed-version Minecraft classes; never creates a game/window. */
public final class InputRegression {
    private static int checks, ids;
    private static final Field DOWN = field("isDown"), CLICKS = field("clickCount");

    private static Field field(String name) {
        try { Field f = KeyMapping.class.getDeclaredField(name); f.setAccessible(true); return f; }
        catch (ReflectiveOperationException e) { throw new AssertionError(e); }
    }
    // Production uses Mixin accessors. This test fixture exposes those same actual MC fields,
    // leaving Minecraft's matching, click-consumption and toggle implementation intact.
    private static final class Mapping extends ToggleKeyMapping implements KeyMappingAccessor {
        Mapping(InputConstants.Key key, boolean toggle) {
            super("bridge.regression." + ++ids, key.getType(), key.getValue(),
                KeyMapping.Category.GAMEPLAY, () -> toggle, false);
        }
        public int passthrough$getClickCount() {
            try { return CLICKS.getInt(this); } catch (IllegalAccessException e) { throw new AssertionError(e); }
        }
        public void passthrough$setClickCount(int n) {
            try { CLICKS.setInt(this, n); } catch (IllegalAccessException e) { throw new AssertionError(e); }
        }
        public void passthrough$setDown(boolean down) {
            try { DOWN.setBoolean(this, down); } catch (IllegalAccessException e) { throw new AssertionError(e); }
        }
    }
    private static Mapping mapping(String name, boolean toggle) {
        return new Mapping(InputConstants.getKey(name), toggle);
    }
    private static MouseButtonEvent mouse(int protocolButton) {
        return new MouseButtonEvent(120, 80, ClientInput.mouseButtonInfo(protocolButton, 65));
    }
    private static void check(boolean result, String message) {
        if (!result) throw new AssertionError(message);
        checks++;
    }
    private static ClassNode bytecode(String className) throws Exception {
        try (InputStream in = InputRegression.class.getClassLoader().getResourceAsStream(className + ".class")) {
            if (in == null) throw new AssertionError("Missing class " + className);
            ClassNode node = new ClassNode(); new ClassReader(in).accept(node, 0); return node;
        }
    }
    private static MethodNode method(ClassNode node, String name) {
        return node.methods.stream().filter(m -> m.name.equals(name)).findFirst().orElseThrow();
    }
    private static int calls(MethodNode method, String owner, String name) {
        int count = 0;
        for (AbstractInsnNode i : method.instructions)
            if (i instanceof MethodInsnNode m && m.owner.equals(owner) && m.name.equals(name)) count++;
        return count;
    }
    private static void mouseBindings() {
        Mapping attack = mapping("key.mouse.left", false), use = mapping("key.mouse.right", false),
            pick = mapping("key.mouse.middle", false);
        check(attack.matchesMouse(mouse(0)), "Left host button must match vanilla attack");
        check(!use.matchesMouse(mouse(0)), "Left must not trigger use");
        check(use.matchesMouse(mouse(1)), "Right host button must match vanilla use/place");
        check(!attack.matchesMouse(mouse(1)), "Right must not trigger attack");
        check(pick.matchesMouse(mouse(2)), "Middle must match vanilla pick");
        check(mouse(0).button() == 1 && mouse(1).button() == 3 && mouse(2).button() == 2,
            "Container click/drag/release must receive SDL button IDs");
        check(mouse(1).modifiers() == 65 && mouse(1).x() == 120, "GUI position/modifiers preserved");
        Mapping reboundAttack = mapping("key.mouse.right", false);
        check(reboundAttack.matchesMouse(mouse(1)), "User mouse rebinding remains native");
        try { mouse(3); throw new AssertionError("Unknown wire button accepted"); }
        catch (IllegalArgumentException expected) { checks++; }
    }
    private static void shortClicksAndHolds() {
        for (String name : new String[]{"key.mouse.left", "key.mouse.right"}) {
            Mapping key = mapping(name, false); RemoteKeyState state = new RemoteKeyState();
            state.set(key, true, false); state.set(key, false, false);
            check(!key.isDown() && key.consumeClick() && !key.consumeClick(), "Short click survives between ticks: " + name);
            state.set(key, true, false); state.set(key, true, false); state.set(key, true, true);
            check(key.isDown() && key.consumeClick() && !key.consumeClick(), "Held input has one press edge: " + name);
            for (int tick = 0; tick < 20; tick++) check(key.isDown(), "Hold persists for native mining/use");
            state.set(key, false, false);
            check(!key.isDown(), "Physical release ends native hold: " + name);
            state.set(key, true, false); state.release();
            check(!key.isDown() && !key.consumeClick(), "Disconnect clears down state and pending click");
            state.set(key, true, true);
            check(!key.isDown() && !key.consumeClick(), "Repeat after disconnect must not restart action");
            state.set(key, true, false);
            check(key.isDown() && key.consumeClick(), "New press after reconnect works");
            state.release();
        }
    }
    private static void togglesAndOffhand() {
        Mapping key = mapping("key.mouse.right", true); RemoteKeyState state = new RemoteKeyState();
        state.set(key, true, false); state.set(key, true, true); state.set(key, true, false);
        check(key.isDown() && key.consumeClick() && !key.consumeClick(), "Duplicates/repeats cannot toggle use back off");
        state.set(key, false, false);
        check(key.isDown(), "Physical release respects native toggle preference");
        state.release();
        check(!key.isDown(), "Focus/screen/disconnect release stops even an already released toggle");
        state.set(key, true, false); state.set(key, false, false); state.set(key, true, false);
        check(!key.isDown(), "Second distinct press switches native toggle off");
        state.release();
        Mapping offhand = mapping("key.keyboard.f", false);
        check(offhand.matches(ClientInput.keyEvent(0x46, 0)), "F reaches native swap-offhand mapping");
        state.set(offhand, true, false); state.set(offhand, false, false);
        check(offhand.consumeClick() && !offhand.consumeClick(), "Offhand swap queued exactly once");
        state.release();
    }
    private static void actualCallsites() throws Exception {
        String base = "dev/rehan/passthrough/client/";
        ClassNode input = bytecode(base + "ClientInput");
        check(calls(method(input, "mouse"), base + "ClientInput", "mouseButtonInfo") == 1,
            "Shared GUI/world mouse event factory must use the tested SDL adapter");
        for (String entry : new String[]{"handle", "release"})
            check(calls(method(input, entry), base + "ClientInput", "mouse") > 0,
                "Click, drag, physical and synthetic release must use shared event factory: " + entry);
        MethodNode vanilla = method(bytecode("net/minecraft/client/Minecraft"), "handleKeybinds");
        check(calls(vanilla, "net/minecraft/client/MouseHandler", "isMouseGrabbed") == 1,
            "Mining redirect must match exactly one real MC 26.3 callsite");
        check(calls(vanilla, "net/minecraft/client/Minecraft", "continueAttack") == 1,
            "Native sustained mining retained");
        check(calls(vanilla, "net/minecraft/client/Minecraft", "startUseItem") > 0,
            "Native block placement / held item use retained");
        MethodNode use = method(bytecode("net/minecraft/client/Minecraft"), "startUseItem");
        check(calls(use, "net/minecraft/world/InteractionHand", "values") == 1,
            "Vanilla use evaluates both hands");
        ClassNode mixin = bytecode(base + "mixin/RemoteInteractionMixin");
        MethodNode hook = method(mixin, "bridge$canContinueAttack");
        check(calls(hook, base + "HostState", "control") == 1 &&
            calls(hook, "net/minecraft/client/gui/Gui", "screen") == 1,
            "Remote mining allowance is gated by live control and screen state");
        check(calls(hook, "net/minecraft/client/MouseHandler", "grabMouse") == 0,
            "Mining fix must not grab the OS mouse");
        try (InputStream in = InputRegression.class.getClassLoader().getResourceAsStream("passthrough.client.mixins.json")) {
            check(new String(in.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8).contains("RemoteInteractionMixin"),
                "Mining hook is registered in deployed client config");
        }
        // Confirm these are still the real vanilla defaults, not just constants copied into the test.
        for (String[] setting : new String[][]{{"keyAttack", "1"}, {"keyUse", "3"}, {"keyPickItem", "2"}}) {
            boolean matched = false;
            for (MethodNode m : bytecode("net/minecraft/client/Options").methods) {
                Integer button = null;
                for (AbstractInsnNode i : m.instructions) {
                    if (i instanceof FieldInsnNode f && f.name.equals("MOUSE") && f.getOpcode() == Opcodes.GETSTATIC) {
                        AbstractInsnNode next = i.getNext();
                        while (next != null && next.getOpcode() < 0) next = next.getNext();
                        if (next != null && next.getOpcode() >= Opcodes.ICONST_0 && next.getOpcode() <= Opcodes.ICONST_5)
                            button = next.getOpcode() - Opcodes.ICONST_0;
                    }
                    if (i instanceof FieldInsnNode f && f.getOpcode() == Opcodes.PUTFIELD && f.name.equals(setting[0]))
                        matched |= button != null && button == Integer.parseInt(setting[1]);
                }
            }
            check(matched, "Actual Options default button changed: " + setting[0]);
        }
    }
    public static void main(String[] args) throws Exception {
        mouseBindings(); shortClicksAndHolds(); togglesAndOffhand(); actualCallsites();
        System.out.println("PASS: " + checks + " input assertions against actual MC 26.3 classes (no game launched).");
    }
}
