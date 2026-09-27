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
    private static final String WINDOWS_DISPLAY = "org/lwjgl/opengl/WindowsDisplay";
    private static final String DISPLAY_MODE = "org/lwjgl/opengl/DisplayMode";

    @Override
    public byte[] transform(String name, String transformedName, byte[] basicClass) {
        if (basicClass == null) {
            return null;
        }

        final boolean patchCraftingManager =
            TARGET_CLASS.equals(name) || TARGET_CLASS.equals(transformedName);
        final boolean patchWindowsDisplay =
            WINDOWS_DISPLAY.equals(name) || WINDOWS_DISPLAY.equals(transformedName);

        if (!patchCraftingManager && !patchWindowsDisplay) {
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

                if (patchWindowsDisplay &&
                    "getCurrentDisplayMode".equals(methodName)
                    && "()Lorg/lwjgl/opengl/DisplayMode;".equals(descriptor)
                    && (access & Opcodes.ACC_NATIVE) != 0) {
                    /*
                     * LWJGL 2.9.4 implements this method as a native Win32 display
                     * query. On Xbox/UWP the underlying call returns ERROR_CALL_NOT_IMPLEMENTED
                     * (120), which aborts Display's static initialization before a GL
                     * context can exist. Replace the native method itself rather than
                     * relying on rewriting its caller; this is stable even if LWJGL's
                     * bytecode layout changes.
                     */
                    patched[0] = true;
                    final int patchedAccess =
                        access & ~Opcodes.ACC_NATIVE & ~Opcodes.ACC_ABSTRACT;

                    MethodVisitor mv = super.visitMethod(
                        patchedAccess, methodName, descriptor, signature, exceptions);
                    mv.visitCode();
                    mv.visitTypeInsn(Opcodes.NEW, DISPLAY_MODE);
                    mv.visitInsn(Opcodes.DUP);
                    mv.visitLdcInsn(Integer.valueOf(1920));
                    mv.visitLdcInsn(Integer.valueOf(1080));
                    mv.visitMethodInsn(
                        Opcodes.INVOKESPECIAL,
                        DISPLAY_MODE,
                        "<init>",
                        "(II)V",
                        false);
                    mv.visitInsn(Opcodes.ARETURN);
                    mv.visitMaxs(4, 0);
                    mv.visitEnd();
                    return null;
                }

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

        System.err.println("[BanditVault] Legacy UWP transformer patched " +
            (patchWindowsDisplay ? "WindowsDisplay" : "ZipFS") +
            " in " + (name != null ? name : transformedName));
        return writer.toByteArray();
    }
}
