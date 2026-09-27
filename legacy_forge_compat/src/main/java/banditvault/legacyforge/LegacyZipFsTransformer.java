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

                MethodVisitor delegate =
                    super.visitMethod(access, methodName, descriptor, signature, exceptions);

                return new MethodVisitor(Opcodes.ASM5, delegate) {
                    @Override
                    public void visitMethodInsn(
                        int opcode, String owner, String calledName,
                        String calledDescriptor, boolean isInterface) {

                        if (patchCraftingManager &&
                            opcode == Opcodes.INVOKESTATIC
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

                        if (patchWindowsDisplay &&
                            "init".equals(methodName)
                            && "()Lorg/lwjgl/opengl/DisplayMode;".equals(descriptor)
                            && opcode == Opcodes.INVOKESTATIC
                            && WINDOWS_DISPLAY.equals(owner)
                            && "getCurrentDisplayMode".equals(calledName)
                            && "()Lorg/lwjgl/opengl/DisplayMode;".equals(calledDescriptor)) {
                            /*
                             * LWJGL 2 asks Windows for the desktop display mode during
                             * static Display initialization. In UWP that native query
                             * returns ERROR_CALL_NOT_IMPLEMENTED (120). Minecraft only
                             * needs a sane initial mode here; fullscreen enumeration is
                             * handled separately. Use public DisplayMode(width,height)
                             * so the result is valid without invoking Win32 display APIs.
                             */
                            super.visitTypeInsn(Opcodes.NEW, DISPLAY_MODE);
                            super.visitInsn(Opcodes.DUP);
                            super.visitLdcInsn(Integer.valueOf(1920));
                            super.visitLdcInsn(Integer.valueOf(1080));
                            super.visitMethodInsn(
                                Opcodes.INVOKESPECIAL,
                                DISPLAY_MODE,
                                "<init>",
                                "(II)V",
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
