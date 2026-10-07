package dev.rehan.passthrough;

import com.google.gson.JsonArray;
import java.util.Map;
import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.Shapes;
import net.minecraft.world.phys.shapes.VoxelShape;

public final class GroundRegression {
    private static int checks;
    private static void check(boolean result,String message) {
        if(!result) throw new AssertionError(message); checks++;
    }
    private static JsonArray boxes(double[]... boxes) {
        JsonArray result=new JsonArray();
        for(double[] box:boxes) { JsonArray b=new JsonArray();for(double v:box)b.add(v);result.add(b); }
        return result;
    }
    private static boolean supported(Map<BlockPos,VoxelShape> shapes,double x,double y,double z) {
        AABB probe=new AABB(x-.3,y-.15,z-.3,x+.3,y+.01,z+.3);
        return shapes.entrySet().stream().anyMatch(e->HostGeometry.footing(e.getValue().move(e.getKey().getX(),e.getKey().getY(),e.getKey().getZ()),probe,y));
    }
    public static void main(String[] args) {
        // A seam crossing and fractional floor height, through the actual parser and MC VoxelShapes.
        Map<BlockPos,VoxelShape> floor=HostGeometry.parse(boxes(new double[]{-.75,63.91,-.75,.25,64.03,.25},
            new double[]{.25,63.91,-.75,1.25,64.03,.25},new double[]{-.75,63.91,.25,.25,64.03,1.25},
            new double[]{.25,63.91,.25,1.25,64.03,1.25}));
        for(double x=-.4;x<1.0;x+=.1) for(double z=-.4;z<1.0;z+=.1)
            check(supported(floor,x,64.05,z),"Fractional floor/seam lost support");
        check(!supported(floor,5,64.05,5),"Unknown floor must not be fabricated");
        var safe=HostGeometry.supportSnapshot(floor,new Vec3(.25,64.05,.25));
        check(!safe.isEmpty() && supported(safe,.25,64.05,.25),"Last safe position keeps its actual floor");
        check(safe.size()<=8,"Recovery snapshot must remain local");
        AABB probe=new AABB(-.3,63.9,-.3,.3,64.01,.3);
        check(!HostGeometry.footing(Shapes.box(-.05,63,0,.05,66,1),probe,64),"Tall wall cannot masquerade as ground");
        check(!HostGeometry.footing(Shapes.box(-1,66,-1,1,66.1,1),probe,64),"Ceiling cannot masquerade as ground");
        check(HostGeometry.parse(boxes()).isEmpty(),"Empty snapshot reported explicitly");
        check(HostGeometry.parse(boxes(new double[]{0,Double.NaN,0,1,2,1})).isEmpty(),"Reject nonfinite geometry");
        check(HostGeometry.parse(boxes(new double[]{0,0,0,100,100,100})).isEmpty(),"Reject oversized geometry");
        JsonArray tooMuch=new JsonArray();for(int i=0;i<4097;i++)tooMuch.add(new JsonArray());
        check(HostGeometry.parse(tooMuch)==null,"Bound snapshot count");
        check(!HostGeometry.needsRecovery(64,64,true,false,false,false),"Waiting on a known floor stays in place");
        check(!HostGeometry.needsRecovery(65.2,64,false,true,false,true),"Native jump remains native");
        check(!HostGeometry.needsRecovery(61,64,false,true,false,true),"Known lower landing allows native fall");
        check(HostGeometry.needsRecovery(62.4,64,false,false,false,true),"Recover before deep void fall");
        check(HostGeometry.needsRecovery(63.9,64,false,false,false,false),"Missing geometry cannot freeze player unsupported");
        check(!HostGeometry.needsRecovery(50,64,false,false,true,false),"Creative flight is not teleported");
        System.out.println("PASS: "+checks+" ground/shape/recovery assertions using actual MC VoxelShapes (no game launched).");
    }
}
