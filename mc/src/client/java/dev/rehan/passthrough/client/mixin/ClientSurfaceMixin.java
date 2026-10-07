package dev.rehan.passthrough.client.mixin;
import dev.rehan.passthrough.HostCollisionBlock;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.MultiPlayerGameMode;
import net.minecraft.core.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
/** Reject the local creative-break prediction too, so server-protected host surfaces never become ghost holes. */
@Mixin(MultiPlayerGameMode.class)
abstract class ClientSurfaceMixin {
    @Inject(method="destroyBlock",at=@At("HEAD"),cancellable=true)
    private void bridge$keepSurface(BlockPos pos,CallbackInfoReturnable<Boolean> ci) {
        var level=Minecraft.getInstance().level;
        if(level!=null && level.getBlockState(pos).is(HostCollisionBlock.INSTANCE)) ci.setReturnValue(false);
    }
}
