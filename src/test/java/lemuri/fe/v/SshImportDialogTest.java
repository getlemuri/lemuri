package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;
import lemuri.be.util.SshConfig;

/** A ProxyJump of the imported configuration refers to a host by name only if that host is in the list. */
class SshImportDialogTest {
    @Test
    void jumpsToHostsNotImportedAreWrittenOut() {
        SshConfig.Entry bastion = new SshConfig.Entry("bastion", "203.0.113.1", "jump", 2222, null, null);
        SshConfig.Entry gw = new SshConfig.Entry("gw", "203.0.113.2", "ops", 22, null, null);
        SshConfig.Entry db = new SshConfig.Entry("db", "10.0.0.5", "app", 22, null, "bastion,gw,other.example");
        Host existing = new Host();
        existing.name = "gw";
        List<Host> imported = new ArrayList<>(List.of(db.toHost("")));
        SshImportDialog.jumpsByName(imported, List.of(bastion, gw, db), List.of(existing));
        // bastion is not imported: written out; gw is already in the list: by name; other.example is not an alias.
        assertEquals("jump@203.0.113.1:2222,gw,other.example", imported.get(0).jump);

        List<Host> both = new ArrayList<>(List.of(db.toHost(""), bastion.toHost("")));
        SshImportDialog.jumpsByName(both, List.of(bastion, gw, db), List.of());
        assertEquals("bastion,ops@203.0.113.2:22,other.example", both.get(0).jump);
    }
}
