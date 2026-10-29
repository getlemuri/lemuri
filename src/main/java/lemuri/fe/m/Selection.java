package lemuri.fe.m;

/** Node chosen in the resource tree: it decides the menu and the content of the central area. */
public sealed interface Selection {
    record Datacenter() implements Selection {}

    /** A tree folder, with its full path (see {@link lemuri.be.util.Folders}). */
    record Folder(String path) implements Selection {}

    record HostItem(String hostId) implements Selection {}

    /** The section of hosts received from contacts and not yet put in one of your own folders. */
    record Received() implements Selection {}

    /** The Team page: contacts, incoming hosts and the local network. */
    record Contacts() implements Selection {}
}
