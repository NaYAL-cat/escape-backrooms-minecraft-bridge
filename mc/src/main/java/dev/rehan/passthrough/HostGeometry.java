package dev.rehan.passthrough;

import com.google.gson.JsonArray;
import java.util.HashMap;
import java.util.Map;
import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.Shapes;
import net.minecraft.world.phys.shapes.VoxelShape;

/** Bounded snapshot conversion and grounding rules, usable without starting a game. */
public final class HostGeometry {
    private HostGeometry() {}
    static Map<BlockPos,VoxelShape> parse(JsonArray boxes) {
        if (boxes.size() > 4096) return null;
        Map<BlockPos,VoxelShape> next = new HashMap<>();
        int visits = 0;
        for (var element : boxes) {
            JsonArray b = element.getAsJsonArray(); if (b.size() != 6) continue;
            double[] v = new double[6]; boolean valid = true;
            for (int i=0;i<6;i++) { v[i]=b.get(i).getAsDouble(); valid &= Double.isFinite(v[i]); }
            if (!valid || v[1]<-64 || v[4]>320 || Math.abs(v[0])>29_000_000 || Math.abs(v[2])>29_000_000) continue;
            if (v[3]<=v[0] || v[4]<=v[1] || v[5]<=v[2] || v[3]-v[0]>16 || v[4]-v[1]>16 || v[5]-v[2]>16) continue;
            for (int x=(int)Math.floor(v[0]);x<Math.ceil(v[3]-1e-7);x++)
            for (int y=(int)Math.floor(v[1]);y<Math.ceil(v[4]-1e-7);y++)
            for (int z=(int)Math.floor(v[2]);z<Math.ceil(v[5]-1e-7);z++) {
                if (++visits>100_000 || next.size()>20_000) return null;
                BlockPos pos = new BlockPos(x,y,z);
                VoxelShape shape = Shapes.box(Math.max(0,v[0]-x),Math.max(0,v[1]-y),Math.max(0,v[2]-z),
                    Math.min(1,v[3]-x),Math.min(1,v[4]-y),Math.min(1,v[5]-z));
                next.merge(pos,shape,Shapes::or);
            }
        }
        return next;
    }
    static boolean footing(VoxelShape worldShape, AABB probe, double feetY) {
        for (AABB box : worldShape.toAabbs())
            if (box.maxY<=feetY+.05 && box.intersects(probe)) return true;
        return false;
    }
    static Map<BlockPos,VoxelShape> supportSnapshot(Map<BlockPos,VoxelShape> source, Vec3 feet) {
        AABB probe = new AABB(feet.x-.31,feet.y-.15,feet.z-.31,feet.x+.31,feet.y+.01,feet.z+.31);
        Map<BlockPos,VoxelShape> result = new HashMap<>();
        for (BlockPos pos : BlockPos.betweenClosed((int)Math.floor(probe.minX),(int)Math.floor(probe.minY),(int)Math.floor(probe.minZ),
                (int)Math.floor(probe.maxX),(int)Math.floor(probe.maxY),(int)Math.floor(probe.maxZ))) {
            VoxelShape shape = source.get(pos);
            if (shape!=null && footing(shape.move(pos.getX(),pos.getY(),pos.getZ()),probe,feet.y))
                result.put(pos.immutable(),shape);
        }
        return Map.copyOf(result);
    }
    static boolean needsRecovery(double feetY, double safeY, boolean supported, boolean landingBelow,
                                 boolean flying, boolean freshGeometry) {
        return !flying && !supported && (!freshGeometry || (feetY<safeY-1.5 && !landingBelow));
    }
}
