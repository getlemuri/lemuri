package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import javafx.scene.control.ComboBox;
import javafx.scene.control.TableView;
import javafx.scene.control.TextField;
import javafx.scene.control.TextInputControl;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;

/** The host list driven as a user would: dialogs, context menus, folders. */
class HostsUiTest extends UiTest {
    @Test
    void addAHostWithAPassword() {
        menu("Hosts", "Add host");
        type(field("web-03"), "cache-01");
        type(field("203.0.113.10"), "203.0.113.50");
        on(() -> dialog().lookupAll(".combo-box").stream().map(n -> (ComboBox<?>) n)
                .filter(c -> c.getItems().contains("Password")).findFirst().map(c -> (ComboBox<String>) c).orElseThrow().setValue("Password"));
        type(field("empty = ask when connecting"), "s3cret");
        press("Add");
        assertFalse(dialogOpen());
        assertTrue(hasHost("cache-01"));
        Host h = host("cache-01");
        assertEquals("203.0.113.50", h.address);
        assertEquals(Host.AuthMethod.PASSWORD, h.authMethod);
        assertEquals("s3cret", vault.password(h.id));
        assertTrue(shows("cache-01"));
    }

    @Test
    void anInvalidHostIsNotSaved() {
        menu("Hosts", "Add host");
        type(field("web-03"), "broken");
        // No address: the dialog must refuse it.
        press("Add");
        assertFalse(hasHost("broken"));
        if (dialogOpen()) press("Cancel");
    }

    @Test
    void aNameAlreadyUsedIsRefused() {
        menu("Hosts", "Add host");
        type(field("web-03"), "DB-01");
        type(field("203.0.113.10"), "203.0.113.77");
        assertTrue(shows("Another host already has this name"));
        assertTrue(get(() -> button("Add").isDisabled()));
        type(field("web-03"), "db-03");
        assertFalse(get(() -> button("Add").isDisabled()));
        press("Cancel");
        // Editing a host keeps its own name, of course.
        menu("db-01", "Edit…");
        assertFalse(shows("Another host already has this name"));
        press("Cancel");
    }

    @Test
    void editChangesTheAddress() {
        menu("db-01", "Edit…");
        type(field("203.0.113.10"), "198.51.100.99");
        press("Save");
        assertEquals("198.51.100.99", host("db-01").address);
    }

    @Test
    void duplicateMakesASecondHost() {
        menu("db-01", "Duplicate…");
        type(field("web-03"), "db-02");
        press("Add");
        assertTrue(hasHost("db-02"));
        assertEquals("203.0.113.20", host("db-02").address);
        assertTrue(hasHost("db-01"));
    }

    @Test
    void moveToAnotherFolder() {
        menu("db-01", "Move to…");
        on(() -> ((ComboBox<String>) dialog().lookup(".combo-box")).setValue("Personal"));
        press("Move");
        assertEquals("Personal", host("db-01").group);
    }

    @Test
    void removeAsksAndRemoves() {
        menu("homelab", "Remove");
        assertTrue(shows("will be removed from the list"));
        press("Cancel");
        assertTrue(hasHost("homelab"));
        menu("homelab", "Remove");
        press("Remove");
        assertFalse(hasHost("homelab"));
    }

    @Test
    void foldersCreateRenameDelete() {
        menu("Hosts", "New folder");
        on(() -> ((TextInputControl) dialog().lookup(".text-field")).setText("Staging"));
        press("Create");
        until(() -> app.state.folderExists("Staging"), "folder created");
        menu("Staging", "Rename…");
        on(() -> ((TextInputControl) dialog().lookup(".text-field")).setText("Test"));
        press("Rename");
        until(() -> app.state.folderExists("Test") && !app.state.folderExists("Staging"), "folder renamed");
        menu("Test", "Delete");
        press("Delete");
        until(() -> !app.state.folderExists("Test"), "folder deleted");
    }

    @Test
    void searchFiltersTheDatacenterList() {
        robot.clickOn(text("Hosts"));
        settle();
        TextField search = (TextField) robot.lookup(n -> n instanceof TextField t && "Search hosts".equals(t.getPromptText())).query();
        robot.clickOn(search).write("api");
        settle();
        TableView<?> table = robot.lookup(".table-view").queryAs(TableView.class);
        until(() -> table.getItems().size() == 1, "one host left");
    }
}
