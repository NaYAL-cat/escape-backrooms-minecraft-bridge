package dev.rehan.passthrough.mixin;
import dev.rehan.passthrough.HostCollisionBlock;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.*;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.*;
@Mixin(ServerPlayerGameMode.class)
abstract class HostSurfaceMixin {
    @Shadow protected ServerLevel level;
    @Inject(method="destroyBlock",at=@At("HEAD"),cancellable=true)
    private void bridge$keepSurface(BlockPos p,CallbackInfoReturnable<Boolean> ci) {
        if(level.getBlockState(p).is(HostCollisionBlock.INSTANCE)) ci.setReturnValue(false);
    }
}
