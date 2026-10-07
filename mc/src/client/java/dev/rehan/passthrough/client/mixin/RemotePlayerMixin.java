package dev.rehan.passthrough.client.mixin;
import dev.rehan.passthrough.BridgeWorld;
import dev.rehan.passthrough.client.HostState;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
@Mixin(LocalPlayer.class)
abstract class RemotePlayerMixin {
    @Inject(method="aiStep",at=@At("HEAD"),cancellable=true)
    private void bridge$waiting(CallbackInfo ci) {
        if(BridgeWorld.ready() && !HostState.control()) {
            LocalPlayer p=(LocalPlayer)(Object)this; p.setDeltaMovement(Vec3.ZERO); p.setNoGravity(true); ci.cancel();
        } else if(BridgeWorld.ready()) ((LocalPlayer)(Object)this).setNoGravity(false);
    }
}
