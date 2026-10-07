package dev.rehan.passthrough.client.mixin;
import dev.rehan.passthrough.client.*;
import net.minecraft.client.Camera;
import net.minecraft.client.DeltaTracker;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
@Mixin(Camera.class)
abstract class CameraMixin {
    @Shadow private float depthFar;
    @Inject(method="update",at=@At("HEAD"))
    private void bridge$begin(DeltaTracker dt,CallbackInfo ci) { HostState.beginFrame(); }
    @Inject(method="update",at=@At("TAIL"))
    private void bridge$far(DeltaTracker dt,CallbackInfo ci) { FrameExporter.setFar(depthFar); }
}
