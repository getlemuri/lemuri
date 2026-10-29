package lemuri.be.storage;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonElement;
import com.google.gson.annotations.SerializedName;
import com.google.gson.reflect.TypeToken;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;
import lemuri.be.dto.Host;
import lemuri.be.service.HostRepository;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;
import lemuri.be.util.PrivateFiles;

/** Hosts and folders in ~/.lemuri/hosts.json. */
public final class JsonHostRepository implements HostRepository {
    private static final Path HOSTS_FILE = Path.of(System.getProperty("user.home"), ".lemuri", "hosts.json");
    private static final Logger LOG = Log.of(JsonHostRepository.class);
    /** The file could be neither read nor moved: saving would overwrite it. */
    private volatile boolean blocked;
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();

    /** File layout: empty folders are saved separately, hosts already carry their own. */
    private static final class SavedData {
        @SerializedName(value = "folders", alternate = "cartelle") List<String> folders = new ArrayList<>();
        List<Host> hosts = new ArrayList<>();
    }

    /**
     * The saved hosts, only read: nothing is rewritten or moved aside, as {@link #load} does. For the command
     * line, which may run while the app is open.
     */
    public static List<Host> peek() throws IOException {
        if (!Files.exists(HOSTS_FILE)) return List.of();
        try {
            JsonElement j = GSON.fromJson(Files.readString(HOSTS_FILE, StandardCharsets.UTF_8), JsonElement.class);
            List<Host> hosts = j == null ? null : j.isJsonArray() ? GSON.fromJson(j, new TypeToken<List<Host>>() {}.getType())
                    : j.isJsonObject() ? GSON.fromJson(j, SavedData.class).hosts : null;
            return hosts != null ? hosts : List.of();
        } catch (RuntimeException e) {
            // Gson throws its own unchecked exceptions on a broken file.
            throw new IOException(e.getMessage(), e);
        }
    }

    @Override
    public Data load() {
        try {
            if (Files.exists(HOSTS_FILE)) {
                JsonElement j = GSON.fromJson(Files.readString(HOSTS_FILE, StandardCharsets.UTF_8), JsonElement.class);
                SavedData d = new SavedData();
                // The first versions saved only the list of hosts.
                if (j.isJsonArray()) d.hosts = GSON.fromJson(j, new TypeToken<List<Host>>() {}.getType());
                else if (j.isJsonObject()) d = GSON.fromJson(j, SavedData.class);
                if (d.hosts == null) d.hosts = new ArrayList<>();
                if (d.folders == null) d.folders = new ArrayList<>();
                Data data = new Data(d.folders, d.hosts);
                // Written by the first versions (Italian field names): rewritten in today's format right away.
                if (!GSON.toJsonTree(d).equals(j)) save(data);
                return data;
            }
        } catch (IOException | RuntimeException e) {
            LOG.warning("Cannot read " + HOSTS_FILE + ": " + e.getMessage());
            return new Data(new ArrayList<>(), new ArrayList<>(), setAside(e));
        }
        return new Data(new ArrayList<>(), new ArrayList<>());
    }

    /**
     * The file cannot be read (edited by hand, from a newer version, disk): starting empty and then saving
     * would erase it. It is moved aside with a new name; if that fails, saving stops.
     */
    private String setAside(Exception e) {
        Path part = HOSTS_FILE.resolveSibling("hosts.json.corrupt-" + LocalDateTime.now()
                .format(DateTimeFormatter.ofPattern("yyyy-MM-dd-HHmmss")));
        try {
            Files.move(HOSTS_FILE, part);
            return I18n.t("hosts.load.kept", e.getMessage(), part);
        } catch (IOException moving) {
            blocked = true;
            return I18n.t("hosts.load.blocked", e.getMessage(), HOSTS_FILE);
        }
    }

    @Override
    public void save(Data data) {
        if (blocked) {
            LOG.warning("Not saving " + HOSTS_FILE + ": it could not be read at startup and was not moved aside");
            return;
        }
        SavedData d = new SavedData();
        d.folders = new ArrayList<>(data.folders());
        d.hosts = new ArrayList<>(data.hosts());
        // Like the vault: readable by the user only, and an interruption halfway leaves the previous file.
        try {
            PrivateFiles.write(HOSTS_FILE, GSON.toJson(d));
        } catch (IOException e) {
            LOG.warning("Cannot save " + HOSTS_FILE + ": " + e.getMessage());
        }
    }
}
