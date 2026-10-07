package dev.rehan.passthrough.client.mixin;

import dev.rehan.passthrough.client.HostState;
import net.minecraft.client.Minecraft;
import net.minecraft.client.MouseHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(Minecraft.class)
abstract class RemoteInteractionMixin {
    // Only the vanilla sustained-mining gate: never grab the OS cursor or change window focus.
    @Redirect(method = "handleKeybinds", at = @At(value = "INVOKE",
        target = "Lnet/minecraft/client/MouseHandler;isMouseGrabbed()Z"), require = 1, allow = 1)
    private boolean bridge$canContinueAttack(MouseHandler mouse) {
        Minecraft mc = (Minecraft) (Object) this;
        return mouse.isMouseGrabbed() || (HostState.control() && mc.gui.screen() == null);
    }
}
