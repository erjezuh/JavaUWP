package banditvault.legacyforge;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Enumeration;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;
import java.util.jar.JarOutputStream;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

/**
 * Produces a LaunchClassLoader class that does not install Mojang's
 * org.lwjgl. class-loader exclusion for the legacy Forge/UWP target.
 *
 * The stock exclusion is added in LaunchClassLoader's constructor. Removing it
 * later from IFMLLoadingPlugin.injectData() can be too late if another Forge
 * coremod touches LWJGL first. Patching the constructor makes the behavior
 * correct from the moment LaunchClassLoader exists.
 */
public final class LegacyLaunchClassLoaderPatcher {
    private static final String TARGET = "net/minecraft/launchwrapper/LaunchClassLoader.class";
    private static final String SELF = "net/minecraft/launchwrapper/LaunchClassLoader";

    private LegacyLaunchClassLoaderPatcher() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException(
                "Usage: LegacyLaunchClassLoaderPatcher <launchwrapper-1.12.jar> <output.class>");
        }

        File jarFile = new File(args[0]);
        File outputFile = new File(args[1]);
        if (!jarFile.isFile()) {
            throw new IOException("LaunchWrapper jar missing: " + jarFile);
        }
        if (outputFile.getParentFile() != null) {
            Files.createDirectories(outputFile.getParentFile().toPath());
        }

        byte[] original;
        try (JarFile input = new JarFile(jarFile)) {
            JarEntry targetEntry = input.getJarEntry(TARGET);
            if (targetEntry == null) {
                throw new IOException("LaunchWrapper jar does not contain " + TARGET);
            }
            original = readAll(input.getInputStream(targetEntry));
        }

        byte[] transformed = patch(original);
        if (transformed == null) {
            throw new IOException(
                "LaunchClassLoader constructor does not contain the stock org.lwjgl. exclusion.");
        }

        Files.write(outputFile.toPath(), transformed);
        System.out.println(
            "[BanditVault] Exported patched LaunchClassLoader class: " + outputFile);
    }

    private static byte[] patch(byte[] original) {
        final boolean[] patched = new boolean[] {false};

        ClassReader reader = new ClassReader(original);
        ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);

        reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
            @Override
            public MethodVisitor visitMethod(
                int access, String name, String descriptor,
                String signature, String[] exceptions) {

                MethodVisitor delegate =
                    super.visitMethod(access, name, descriptor, signature, exceptions);

                if (!"<init>".equals(name)) {
                    return delegate;
                }

                return new MethodVisitor(Opcodes.ASM5, delegate) {
                    private boolean pendingLwjglConstant;

                    private void flushPending() {
                        if (pendingLwjglConstant) {
                            delegate.visitLdcInsn("org.lwjgl.");
                            pendingLwjglConstant = false;
                        }
                    }

                    @Override
                    public void visitLdcInsn(Object cst) {
                        if ("org.lwjgl.".equals(cst)) {
                            pendingLwjglConstant = true;
                            return;
                        }
                        flushPending();
                        delegate.visitLdcInsn(cst);
                    }

                    @Override
                    public void visitMethodInsn(
                        int opcode, String owner, String methodName,
                        String methodDescriptor, boolean isInterface) {

                        if (pendingLwjglConstant
                            && opcode == Opcodes.INVOKEVIRTUAL
                            && SELF.equals(owner)
                            && "addClassLoaderExclusion".equals(methodName)
                            && "(Ljava/lang/String;)V".equals(methodDescriptor)) {
                            pendingLwjglConstant = false;
                            patched[0] = true;
                            return;
                        }

                        flushPending();
                        delegate.visitMethodInsn(
                            opcode, owner, methodName, methodDescriptor, isInterface);
                    }

                    @Override
                    public void visitInsn(int opcode) {
                        flushPending();
                        delegate.visitInsn(opcode);
                    }

                    @Override
                    public void visitIntInsn(int opcode, int operand) {
                        flushPending();
                        delegate.visitIntInsn(opcode, operand);
                    }

                    @Override
                    public void visitVarInsn(int opcode, int var) {
                        flushPending();
                        delegate.visitVarInsn(opcode, var);
                    }

                    @Override
                    public void visitTypeInsn(int opcode, String type) {
                        flushPending();
                        delegate.visitTypeInsn(opcode, type);
                    }

                    @Override
                    public void visitFieldInsn(
                        int opcode, String owner, String fieldName, String fieldDescriptor) {
                        flushPending();
                        delegate.visitFieldInsn(opcode, owner, fieldName, fieldDescriptor);
                    }

                    @Override
                    public void visitJumpInsn(int opcode, org.objectweb.asm.Label label) {
                        flushPending();
                        delegate.visitJumpInsn(opcode, label);
                    }

                    @Override
                    public void visitTryCatchBlock(
                        org.objectweb.asm.Label start,
                        org.objectweb.asm.Label end,
                        org.objectweb.asm.Label handler,
                        String type) {
                        flushPending();
                        delegate.visitTryCatchBlock(start, end, handler, type);
                    }

                    @Override
                    public void visitLabel(org.objectweb.asm.Label label) {
                        flushPending();
                        delegate.visitLabel(label);
                    }

                    @Override
                    public void visitLineNumber(
                        int line, org.objectweb.asm.Label start) {
                        flushPending();
                        delegate.visitLineNumber(line, start);
                    }

                    @Override
                    public void visitLocalVariable(
                        String variableName, String variableDescriptor,
                        String variableSignature, org.objectweb.asm.Label start,
                        org.objectweb.asm.Label end, int index) {
                        flushPending();
                        delegate.visitLocalVariable(
                            variableName, variableDescriptor, variableSignature,
                            start, end, index);
                    }

                    @Override
                    public void visitIincInsn(int var, int increment) {
                        flushPending();
                        delegate.visitIincInsn(var, increment);
                    }

                    @Override
                    public void visitTableSwitchInsn(
                        int min, int max, org.objectweb.asm.Label dflt,
                        org.objectweb.asm.Label... labels) {
                        flushPending();
                        delegate.visitTableSwitchInsn(min, max, dflt, labels);
                    }

                    @Override
                    public void visitLookupSwitchInsn(
                        org.objectweb.asm.Label dflt, int[] keys,
                        org.objectweb.asm.Label[] labels) {
                        flushPending();
                        delegate.visitLookupSwitchInsn(dflt, keys, labels);
                    }

                    @Override
                    public void visitMultiANewArrayInsn(String descriptor, int dims) {
                        flushPending();
                        delegate.visitMultiANewArrayInsn(descriptor, dims);
                    }

                    @Override
                    public void visitFrame(int type, int nLocal, Object[] local,
                                           int nStack, Object[] stack) {
                        flushPending();
                        delegate.visitFrame(type, nLocal, local, nStack, stack);
                    }

                    @Override
                    public void visitInvokeDynamicInsn(
                        String methodName, String methodDescriptor,
                        org.objectweb.asm.Handle bootstrapMethodHandle,
                        Object... bootstrapMethodArguments) {
                        flushPending();
                        delegate.visitInvokeDynamicInsn(
                            methodName, methodDescriptor,
                            bootstrapMethodHandle, bootstrapMethodArguments);
                    }

                    @Override
                    public void visitEnd() {
                        flushPending();
                        delegate.visitEnd();
                    }
                };
            }
        }, 0);

        return patched[0] ? writer.toByteArray() : null;
    }

    private static byte[] readAll(InputStream input) throws IOException {
        try (InputStream in = input) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }
}
