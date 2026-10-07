package dev.rehan.passthrough.mixin;
import dev.rehan.passthrough.HostEntities;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.npc.villager.Villager;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
/** A combat proxy is not a shop. Right click must remain available to shields, bows and items. */
@Mixin(Villager.class)
abstract class HostProxyInteractionMixin {
    @Inject(method="mobInteract",at=@At("HEAD"),cancellable=true)
    private void bridge$pass(Player player,InteractionHand hand,CallbackInfoReturnable<InteractionResult> ci) {
        if(HostEntities.isProxy((Villager)(Object)this)) ci.setReturnValue(InteractionResult.PASS);
    }
}
