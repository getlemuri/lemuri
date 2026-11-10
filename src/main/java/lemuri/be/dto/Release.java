package lemuri.be.dto;

/**
 * A published version of Lemuri, as GitHub describes it: the tag (v0.2.0), the version number (0.2.0),
 * the release notes (the section of CHANGELOG.md, in Markdown) and the page of the release.
 */
public record Release(String tag, String version, String notes, String url) {}
