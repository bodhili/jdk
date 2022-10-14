/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

import java.nio.file.Path;
import jdk.jpackage.internal.ApplicationLayout;
import jdk.jpackage.test.JPackageCommand;
import jdk.jpackage.test.TKit;
import jdk.jpackage.test.PackageTest;
import jdk.jpackage.test.PackageType;
import jdk.jpackage.test.MacHelper;
import jdk.jpackage.test.Annotations.Test;
import jdk.jpackage.test.Annotations.Parameter;

/**
 * Tests generation of dmg and pkg from signed predefined app image which was
 * signed using two step process (generate app image and then signed using
 * --app-image and --mac-sign). Test will generate pkg and verifies its
 * signature. It verifies that dmg is not signed, but app image inside dmg
 * is signed. This test requires that the machine is configured with test
 * certificate for "Developer ID Installer: jpackage.openjdk.java.net" in
 * jpackagerTest keychain with always allowed access to this keychain for user
 * which runs test.
 * note:
 * "jpackage.openjdk.java.net" can be over-ridden by systerm property
 * "jpackage.mac.signing.key.user.name", and
 * "jpackagerTest" can be over-ridden by system property
 * "jpackage.mac.signing.keychain"
 */

/*
 * @test
 * @summary jpackage with --type pkg,dmg --app-image
 * @library ../helpers
 * @library /test/lib
 * @library base
 * @key jpackagePlatformPackage
 * @build SigningBase
 * @build SigningCheck
 * @build jtreg.SkippedException
 * @build jdk.jpackage.test.*
 * @build SigningPackageFromTwoStepAppImageTest
 * @modules jdk.jpackage/jdk.jpackage.internal
 * @requires (os.family == "mac")
 * @run main/othervm/timeout=720 -Xmx512m jdk.jpackage.test.Main
 *  --jpt-run=SigningPackageFromTwoStepAppImageTest
 */
public class SigningPackageFromTwoStepAppImageTest {

    private static void verifyPKG(JPackageCommand cmd) {
        if (!cmd.hasArgument("--mac-sign")) {
            return; // Nothing to check if not signed
        }

        Path outputBundle = cmd.outputBundle();
        SigningBase.verifyPkgutil(outputBundle);
        SigningBase.verifySpctl(outputBundle, "install");
    }

    private static void verifyDMG(JPackageCommand cmd) {
        // DMG always unsigned, so we will check it
        Path outputBundle = cmd.outputBundle();
        SigningBase.verifyCodesign(outputBundle, false);
    }

    private static void verifyAppImageInDMG(JPackageCommand cmd) {
        MacHelper.withExplodedDmg(cmd, dmgImage -> {
            // We will be called with all folders in DMG since JDK-8263155, but
            // we only need to verify app.
            if (dmgImage.endsWith(cmd.name() + ".app")) {
                Path launcherPath = ApplicationLayout.platformAppImage()
                    .resolveAt(dmgImage).launchersDirectory().resolve(cmd.name());
                SigningBase.verifyCodesign(launcherPath, true);
                SigningBase.verifyCodesign(dmgImage, true);
                SigningBase.verifySpctl(dmgImage, "exec");
            }
        });
    }

    @Test
    @Parameter("true")
    @Parameter("false")
    public static void test(boolean signAppImage) throws Exception {
        SigningCheck.checkCertificates();

        Path appimageOutput = TKit.createTempDirectory("appimage");

        // Generate app image. Signed or unsigned based on test
        // parameter. We should able to sign predfined app images
        // which are signed or unsigned.
        JPackageCommand appImageCmd = JPackageCommand.helloAppImage()
                .setArgumentValue("--dest", appimageOutput);
        if (signAppImage) {
            appImageCmd.addArguments("--mac-sign", "--mac-signing-key-user-name",
                    SigningBase.DEV_NAME, "--mac-signing-keychain",
                    SigningBase.KEYCHAIN);
        }

        // Generate app image
        appImageCmd.executeAndAssertHelloAppImageCreated();

        // Double check if it is signed or unsigned based on signAppImage
        SigningBase.verifyAppImageSignature(appImageCmd, signAppImage);

        // Sign app image
        JPackageCommand appImageSignedCmd = new JPackageCommand();
        appImageSignedCmd.setPackageType(PackageType.IMAGE)
            .addArguments("--app-image", appImageCmd.outputBundle().toAbsolutePath())
            .addArguments("--mac-sign")
            .addArguments("--mac-signing-key-user-name", SigningBase.DEV_NAME)
            .addArguments("--mac-signing-keychain", SigningBase.KEYCHAIN);
        appImageSignedCmd.executeAndAssertImageCreated();

        // Should be signed app image
        SigningBase.verifyAppImageSignature(appImageCmd, true);

        new PackageTest()
                .forTypes(PackageType.MAC)
                .addInitializer(cmd -> {
                    cmd.addArguments("--app-image", appImageCmd.outputBundle());
                    cmd.removeArgumentWithValue("--input");
                    if (signAppImage) {
                        cmd.addArguments("--mac-sign",
                                "--mac-signing-key-user-name",
                                SigningBase.DEV_NAME,
                                "--mac-signing-keychain",
                                SigningBase.KEYCHAIN);
                    }
                })
                .forTypes(PackageType.MAC_PKG)
                .addBundleVerifier(
                    SigningPackageFromTwoStepAppImageTest::verifyPKG)
                .forTypes(PackageType.MAC_DMG)
                .addBundleVerifier(
                    SigningPackageFromTwoStepAppImageTest::verifyDMG)
                .addBundleVerifier(
                    SigningPackageFromTwoStepAppImageTest::verifyAppImageInDMG)
                .run();
    }
}
