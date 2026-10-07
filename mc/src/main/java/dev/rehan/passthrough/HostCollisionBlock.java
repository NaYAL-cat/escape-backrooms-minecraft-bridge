package dev.rehan.passthrough;
import net.minecraft.core.*;
import net.minecraft.core.registries.*;
import net.minecraft.resources.*;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.block.state.*;
import net.minecraft.world.phys.shapes.*;

/** Invisible host surfaces. Each cell retains sub-block bounds instead of becoming a full barrier cube. */
public final class HostCollisionBlock extends Block {
    public static HostCollisionBlock INSTANCE;
    public HostCollisionBlock(Properties p) { super(p); }
    public static void register() {
        ResourceKey<Block> key=ResourceKey.create(Registries.BLOCK,Identifier.fromNamespaceAndPath(Passthrough.ID,"host_surface"));
        INSTANCE=Registry.register(BuiltInRegistries.BLOCK,key,new HostCollisionBlock(Properties.of().setId(key)
            .strength(-1,3600000).noLootTable().noOcclusion().dynamicShape().noTerrainParticles()
            .isSuffocating((s,l,p)->false).isViewBlocking((s,l,p,a)->false)));
    }
    @Override protected RenderShape getRenderShape(BlockState s) { return RenderShape.INVISIBLE; }
    @Override protected VoxelShape getShape(BlockState s,BlockGetter l,BlockPos p,CollisionContext c) { return BridgeWorld.shape(p); }
    @Override protected VoxelShape getCollisionShape(BlockState s,BlockGetter l,BlockPos p,CollisionContext c) { return BridgeWorld.shape(p); }
    @Override protected VoxelShape getVisualShape(BlockState s,BlockGetter l,BlockPos p,CollisionContext c) { return Shapes.empty(); }
}
