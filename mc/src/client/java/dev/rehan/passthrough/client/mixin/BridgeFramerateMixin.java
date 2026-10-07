package dev.rehan.passthrough.client.mixin;

import com.mojang.blaze3d.platform.FramerateLimitTracker;
import dev.rehan.passthrough.client.HostState;
import net.minecraft.client.Options;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** The host owns focus while MC exports; minimized/idle throttling would starve the shared picture. */
@Mixin(FramerateLimitTracker.class)
abstract class BridgeFramerateMixin {
    @Inject(method="getFramerateLimit", at=@At("HEAD"), cancellable=true)
    private void bridge$uncapped(CallbackInfoReturnable<Integer> ci) {
        if (HostState.exporting()) ci.setReturnValue(Options.UNLIMITED_FRAMERATE_CUTOFF);
    }
    @Inject(method="isHeavilyThrottled", at=@At("HEAD"), cancellable=true)
    private void bridge$keepRendering(CallbackInfoReturnable<Boolean> ci) {
        if (HostState.exporting()) ci.setReturnValue(false);
    }
}
