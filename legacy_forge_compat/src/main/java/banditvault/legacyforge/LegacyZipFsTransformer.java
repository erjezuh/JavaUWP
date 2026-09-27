package banditvault.legacyforge;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

public final class LegacyZipFsTransformer implements net.minecraft.launchwrapper.IClassTransformer {
    private static final String TARGET_CLASS = "net/minecraft/item/crafting/CraftingManager";
    private static final String FILE_SYSTEMS = "java/nio/file/FileSystems";
    private static final String URI_FS_DESC =
        "(Ljava/net/URI;Ljava/util/Map;)Ljava/nio/file/FileSystem;";
    private static final String BRIDGE = "banditvault/legacyforge/LegacyZipFsBridge";

    @Override
    public byte[] transform(String name, String transformedName, byte[] basicClass) {
        if (basicClass == null ||
            !(TARGET_CLASS.equals(name) || TARGET_CLASS.equals(transformedName))) {
            return basicClass;
        }

        final boolean[] patched = new boolean[] {false};
        ClassReader reader = new ClassReader(basicClass);
        ClassWriter writer = new ClassWriter(reader, 0);

        reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
            @Override
            public MethodVisitor visitMethod(
                int access, String methodName, String descriptor,
                String signature, String[] exceptions) {

                MethodVisitor delegate =
                    super.visitMethod(access, methodName, descriptor, signature, exceptions);

                return new MethodVisitor(Opcodes.ASM5, delegate) {
                    @Override
                    public void visitMethodInsn(
                        int opcode, String owner, String calledName,
                        String calledDescriptor, boolean isInterface) {

                        if (opcode == Opcodes.INVOKESTATIC
                            && FILE_SYSTEMS.equals(owner)
                            && "newFileSystem".equals(calledName)
                            && URI_FS_DESC.equals(calledDescriptor)) {
                            super.visitMethodInsn(
                                Opcodes.INVOKESTATIC,
                                BRIDGE,
                                "newFileSystem",
                                URI_FS_DESC,
                                false);
                            patched[0] = true;
                            return;
                        }

                        super.visitMethodInsn(
                            opcode, owner, calledName, calledDescriptor, isInterface);
                    }
                };
            }
        }, 0);

        if (!patched[0]) {
            return basicClass;
        }

        return writer.toByteArray();
    }
}
