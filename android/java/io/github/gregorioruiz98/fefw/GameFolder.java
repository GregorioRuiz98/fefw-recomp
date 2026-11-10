// SPDX-License-Identifier: GPL-3.0-or-later
package io.github.gregorioruiz98.fefw;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import org.json.JSONObject;
import java.io.InputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Map;

final class GameFolder {
    static final class Document {
        final Uri uri; final boolean directory;
        Document(Uri uri, boolean directory) { this.uri = uri; this.directory = directory; }
    }

    static Map<String, Document> children(Context context, Uri tree, Uri parent) throws Exception {
        Map<String, Document> result = new HashMap<>();
        Uri query = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getDocumentId(parent));
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = context.getContentResolver().query(query, columns, null, null, null)) {
            if (cursor == null) throw new IOException("The folder provider could not be read. Select a local folder again.");
            while (cursor.moveToNext()) {
                if (Thread.currentThread().isInterrupted()) throw new IOException("Validation cancelled");
                String name = cursor.getString(1).toLowerCase(java.util.Locale.ROOT);
                Document item = new Document(DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0)),
                    DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2)));
                if (result.put(name, item) != null) throw new IOException("Ambiguous duplicate folder entry");
                if (result.size() > 20000) throw new IOException("Select the game folder more precisely");
            }
        }
        return result;
    }

    static Document directory(Map<String, Document> entries, String name) throws IOException {
        Document item = entries.get(name);
        if (item == null || !item.directory) throw new IOException("Required folder is missing: " + name);
        return item;
    }

    static String hex(byte[] data, int start, int size) {
        StringBuilder text = new StringBuilder();
        for (int i = start; i < start + size; ++i) text.append(String.format(java.util.Locale.ROOT, "%02x", data[i] & 255));
        return text.toString();
    }

    static boolean headerMatches(byte[] header, String buildId) {
        return header.length >= 0x60 && header[0] == 'N' && header[1] == 'S' && header[2] == 'O' && header[3] == '0'
            && hex(header, 0x40, 32).equalsIgnoreCase(buildId);
    }

    interface Progress { void update(String text); }

    static String validate(Context context, Uri tree, Progress progress) throws Exception {
        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        Map<String, Document> entries = children(context, tree, root);
        if (!entries.containsKey("exefs")) {
            Document updates = directory(entries, "updates");
            Document update = directory(children(context, tree, updates.uri), "131072");
            entries = children(context, tree, update.uri);
        }
        Document exefs = directory(entries, "exefs");
        directory(entries, "romfs");
        Map<String, Document> modules = children(context, tree, exefs.uri);
        JSONObject supported;
        try (InputStream input = context.getAssets().open("supported-build.json")) {
            java.io.ByteArrayOutputStream data = new java.io.ByteArrayOutputStream();
            byte[] block = new byte[4096]; int read;
            while ((read = input.read(block)) != -1) data.write(block, 0, read);
            supported = new JSONObject(data.toString("UTF-8")).getJSONObject("modules");
        }
        for (String name : new String[]{"main", "rtld", "sdk", "subsdk0", "subsdk1", "subsdk2"}) {
            progress.update("Verifying " + name + " (SHA-256)…");
            Document file = modules.get(name);
            if (file == null || file.directory) throw new IOException("Required ExeFS module is missing: " + name);
            JSONObject identity = supported.getJSONObject(name);
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            try (InputStream input = context.getContentResolver().openInputStream(file.uri)) {
                if (input == null) throw new IOException("Cannot open module " + name);
                byte[] header = new byte[0x100]; int used = 0;
                while (used < header.length) {
                    int read = input.read(header, used, header.length - used);
                    if (read < 0) break;
                    used += read;
                }
                if (used != header.length || !headerMatches(header, identity.getString("build_id")))
                    throw new IOException("Unsupported NSO or Build ID: " + name);
                sha.update(header);
                byte[] block = new byte[256 * 1024]; int read;
                while ((read = input.read(block)) != -1) {
                    if (Thread.currentThread().isInterrupted()) throw new IOException("Validation cancelled");
                    sha.update(block, 0, read);
                }
                byte[] digest = sha.digest();
                if (!hex(digest, 0, digest.length).equalsIgnoreCase(identity.getString("sha256")))
                    throw new IOException("Module SHA-256 does not match the supported update: " + name);
            }
        }
        return "PASS: all six ExeFS modules match update 131072; RomFS folder found. The Android game runtime is still being ported.";
    }
    private GameFolder() {}
}
