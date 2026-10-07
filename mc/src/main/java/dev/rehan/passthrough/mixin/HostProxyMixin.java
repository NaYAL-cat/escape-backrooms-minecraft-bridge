package dev.rehan.passthrough.mixin;
import dev.rehan.passthrough.HostEntities;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.damagesource.DamageSource;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.*;
@Mixin(LivingEntity.class)
abstract class HostProxyMixin {
    @Unique private float bridge$health;
    @Inject(method="hurtServer",at=@At("HEAD"))
    private void bridge$before(ServerLevel l,DamageSource s,float amount,CallbackInfoReturnable<Boolean> ci) { bridge$health=((LivingEntity)(Object)this).getHealth(); }
    @Inject(method="hurtServer",at=@At("RETURN"))
    private void bridge$damage(ServerLevel l,DamageSource s,float amount,CallbackInfoReturnable<Boolean> ci) {
        if(ci.getReturnValueZ()) HostEntities.hit((LivingEntity)(Object)this,s,Math.max(0,bridge$health-((LivingEntity)(Object)this).getHealth()));
    }
    @Inject(method="isPushable",at=@At("HEAD"),cancellable=true)
    private void bridge$push(CallbackInfoReturnable<Boolean> ci) {
        if(HostEntities.isProxy((LivingEntity)(Object)this)) ci.setReturnValue(false);
    }
}
