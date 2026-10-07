package dev.rehan.passthrough.client.mixin;
import dev.rehan.passthrough.client.ClientInput;
import net.minecraft.client.MouseHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
@Mixin(MouseHandler.class)
abstract class RemoteMouseMixin {
    @Inject(method="xpos",at=@At("HEAD"),cancellable=true)
    private void bridge$x(CallbackInfoReturnable<Double> ci) { if(ClientInput.remoteCursorVisible()) ci.setReturnValue(ClientInput.pixelX()); }
    @Inject(method="ypos",at=@At("HEAD"),cancellable=true)
    private void bridge$y(CallbackInfoReturnable<Double> ci) { if(ClientInput.remoteCursorVisible()) ci.setReturnValue(ClientInput.pixelY()); }
    @Inject(method="getScaledXPos(Lcom/mojang/blaze3d/platform/Window;)D",at=@At("HEAD"),cancellable=true)
    private void bridge$scaledX(com.mojang.blaze3d.platform.Window w,CallbackInfoReturnable<Double> ci) { if(ClientInput.remoteCursorVisible()) ci.setReturnValue((double)ClientInput.remoteCursorX(w.getGuiScaledWidth())); }
    @Inject(method="getScaledYPos(Lcom/mojang/blaze3d/platform/Window;)D",at=@At("HEAD"),cancellable=true)
    private void bridge$scaledY(com.mojang.blaze3d.platform.Window w,CallbackInfoReturnable<Double> ci) { if(ClientInput.remoteCursorVisible()) ci.setReturnValue((double)ClientInput.remoteCursorY(w.getGuiScaledHeight())); }
}
