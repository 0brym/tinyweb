#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DEFAULT_URL "https://github.com/0brym/tinyweb"
#define MAX_URL_LENGTH 2048
#define MAX_TITLE_LENGTH 1024
#define MAX_LINE_LENGTH 4096

typedef struct {
    WebKitWebView *web_view;
    GtkWidget *url_entry;
    GtkListStore *bookmarks_store;
    gchar *bookmarks_path;
} BrowserData;

// URL validation for security
static gboolean is_valid_url(const char *url) {
    if (!url || strlen(url) > MAX_URL_LENGTH) {
        return FALSE;
    }
    
    // Check for JavaScript or data URLs (potential XSS vectors)
    if (g_str_has_prefix(url, "javascript:") || 
        g_str_has_prefix(url, "data:")) {
        return FALSE;
    }
    
    // Only allow common protocols
    if (g_str_has_prefix(url, "http://") || 
        g_str_has_prefix(url, "https://") ||
        g_str_has_prefix(url, "file://") ||
        g_str_has_prefix(url, "about:")) {
        return TRUE;
    }
    
    // For other inputs, they'll get http:// prefix - check for dangerous chars
    const char *invalid_chars = "<>\"'\\(){}[];";
    for (int i = 0; i < strlen(invalid_chars); i++) {
        if (strchr(url, invalid_chars[i]) != NULL) {
            return FALSE;
        }
    }
    
    return TRUE;
}

// Safe string copy with bounds checking
static void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    size_t src_len = strlen(src);
    size_t copy_len = src_len < (dest_size - 1) ? src_len : (dest_size - 1);
    
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

// Sanitize strings for safe file storage
static gchar *sanitize_string(const gchar *input) {
    if (!input) return g_strdup("");
    
    GString *sanitized = g_string_new(NULL);
    for (int i = 0; input[i]; i++) {
        if (input[i] == '|' || input[i] == '\n' || input[i] == '\r') {
            g_string_append_c(sanitized, '_');
        } else {
            g_string_append_c(sanitized, input[i]);
        }
    }
    
    return g_string_free(sanitized, FALSE);
}

// Get secure path to bookmarks file
static gchar *get_bookmarks_path() {
    const gchar *config_dir = g_get_user_config_dir();
    gchar *tinyweb_dir = g_build_filename(config_dir, "tinyweb", NULL);
    
    // Create directory if it doesn't exist
    if (g_mkdir_with_parents(tinyweb_dir, 0700) != 0) {
        g_warning("Failed to create config directory %s", tinyweb_dir);
        g_free(tinyweb_dir);
        return NULL;
    }
    
    gchar *bookmarks_path = g_build_filename(tinyweb_dir, "bookmarks.txt", NULL);
    g_free(tinyweb_dir);
    
    return bookmarks_path;
}

// Callback to close the application
static void on_destroy(GtkWidget *widget, gpointer data) {
    BrowserData *browser_data = (BrowserData *)data;
    
    // Close the WebView properly first
    if (browser_data && browser_data->web_view) {
        // Disconnect signals
        g_signal_handlers_disconnect_matched(browser_data->web_view, 
                                          G_SIGNAL_MATCH_DATA, 
                                          0, 0, NULL, NULL, 
                                          browser_data);
                                          
        // Load about:blank to stop any active processes
        webkit_web_view_load_uri(browser_data->web_view, "about:blank");
        
        // Process pending events
        while (gtk_events_pending())
            gtk_main_iteration();
    }
    
    // Clean up resources
    if (browser_data && browser_data->bookmarks_path) {
        g_free(browser_data->bookmarks_path);
    }
    
    // Now exit
    gtk_main_quit();
}

// Navigate to URL from address bar
static void navigate_to_url(GtkWidget *widget, gpointer data) {
    BrowserData *browser_data = (BrowserData *)data;
    const gchar *url = gtk_entry_get_text(GTK_ENTRY(browser_data->url_entry));
    
    // Add http:// prefix if missing
    gchar *full_url;
    if (strstr(url, "://") == NULL) {
        full_url = g_strconcat("http://", url, NULL);
    } else {
        full_url = g_strdup(url);
    }
    
    // Validate URL before loading
    if (!is_valid_url(full_url)) {
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Invalid or potentially unsafe URL"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(full_url);
        return;
    }
    
    webkit_web_view_load_uri(browser_data->web_view, full_url);
    g_free(full_url);
}

// Navigation button callbacks
static void go_back(GtkWidget *widget, gpointer data) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(data);
    if (webkit_web_view_can_go_back(web_view)) {
        webkit_web_view_go_back(web_view);
    }
}

static void go_forward(GtkWidget *widget, gpointer data) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(data);
    if (webkit_web_view_can_go_forward(web_view)) {
        webkit_web_view_go_forward(web_view);
    }
}

static void refresh_page(GtkWidget *widget, gpointer data) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(data);
    webkit_web_view_reload(web_view);
}

static void go_home(GtkWidget *widget, gpointer data) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(data);
    const gchar *home_url = g_object_get_data(G_OBJECT(widget), "home-url");
    
    if (is_valid_url(home_url)) {
        webkit_web_view_load_uri(web_view, home_url);
    } else {
        webkit_web_view_load_uri(web_view, DEFAULT_URL);
    }
}

// Handle TLS errors
static gboolean on_load_failed_with_tls_errors(WebKitWebView *web_view,
                                           gchar *failing_uri,
                                           GTlsCertificate *certificate,
                                           GTlsCertificateFlags errors,
                                           gpointer user_data) {
    GtkWidget *dialog;
    GtkWidget *window = gtk_widget_get_toplevel(GTK_WIDGET(web_view));
    gint response;
    
    dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                  GTK_DIALOG_MODAL,
                                  GTK_MESSAGE_WARNING,
                                  GTK_BUTTONS_YES_NO,
                                  "The website's security certificate is not trusted:\n%s\n\nDo you want to continue?",
                                  failing_uri);
    
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        // Create a new WebKitWebContext with certificate exception
        WebKitWebContext *context = webkit_web_context_new();
        webkit_web_context_allow_tls_certificate_for_host(context, certificate, failing_uri);
        
        // Load the URI in a new WebView with this context
        WebKitWebView *new_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
        webkit_web_view_load_uri(new_view, failing_uri);
        
        // Replace the existing WebView
        GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(web_view));
        if (parent) {
            gtk_container_remove(GTK_CONTAINER(parent), GTK_WIDGET(web_view));
            gtk_container_add(GTK_CONTAINER(parent), GTK_WIDGET(new_view));
            gtk_widget_show(GTK_WIDGET(new_view));
        }
        
        return TRUE;
    }
    
    return TRUE; // We handled the error
}

// Update address bar when page loads
static void web_view_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer data) {
    if (event == WEBKIT_LOAD_FINISHED) {
        BrowserData *browser_data = (BrowserData *)data;
        const gchar *uri = webkit_web_view_get_uri(web_view);
        gtk_entry_set_text(GTK_ENTRY(browser_data->url_entry), uri ? uri : "");
    }
}

// Load bookmarks from file
static void load_bookmarks(GtkListStore *store, const gchar *bookmarks_path) {
    if (!bookmarks_path) return;
    
    FILE *file = fopen(bookmarks_path, "r");
    if (!file) return;
    
    char line[MAX_LINE_LENGTH];
    char title[MAX_TITLE_LENGTH];
    char url[MAX_URL_LENGTH];
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
            if (len > 1 && line[len-2] == '\r') {
                line[len-2] = '\0';
            }
        }
        
        // Split into title and URL with size limits
        char *delim = strchr(line, '|');
        if (delim) {
            *delim = '\0';
            safe_strcpy(title, line, MAX_TITLE_LENGTH);
            safe_strcpy(url, delim + 1, MAX_URL_LENGTH);
            
            // Only add if URL is valid
            if (is_valid_url(url)) {
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter, 0, title, 1, url, -1);
            }
        }
    }
    
    fclose(file);
}

// Save bookmarks to file
static void save_bookmarks(GtkListStore *store, const gchar *bookmarks_path) {
    if (!bookmarks_path) {
        g_warning("No bookmarks path specified");
        return;
    }
    
    FILE *file = fopen(bookmarks_path, "w");
    if (!file) {
        g_warning("Failed to save bookmarks to %s", bookmarks_path);
        return;
    }
    
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    
    while (valid) {
        gchar *title, *url;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &title, 1, &url, -1);
        
        // Sanitize data before saving
        gchar *safe_title = sanitize_string(title);
        gchar *safe_url = sanitize_string(url);
        
        fprintf(file, "%s|%s\n", safe_title, safe_url);
        
        g_free(safe_title);
        g_free(safe_url);
        g_free(title);
        g_free(url);
        
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    
    fclose(file);
    // Set secure permissions on the file
    chmod(bookmarks_path, 0600);
}

// Add current page to bookmarks
static void add_bookmark(GtkWidget *widget, gpointer data) {
    BrowserData *browser_data = (BrowserData *)data;
    const gchar *uri = webkit_web_view_get_uri(browser_data->web_view);
    const gchar *title = webkit_web_view_get_title(browser_data->web_view);
    
    if (uri && is_valid_url(uri)) {
        // Limit title length
        gchar *safe_title;
        if (title) {
            safe_title = g_strndup(title, MAX_TITLE_LENGTH - 1);
        } else {
            safe_title = g_strndup(uri, MAX_TITLE_LENGTH - 1);
        }
        
        GtkTreeIter iter;
        gtk_list_store_append(browser_data->bookmarks_store, &iter);
        gtk_list_store_set(browser_data->bookmarks_store, &iter,
                         0, safe_title,
                         1, uri,
                         -1);
        
        g_free(safe_title);
        
        save_bookmarks(browser_data->bookmarks_store, browser_data->bookmarks_path);
    }
}

// Delete selected bookmark
static void delete_bookmark(GtkWidget *widget, gpointer data) {
    GtkTreeView *tree_view = GTK_TREE_VIEW(data);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        BrowserData *browser_data = g_object_get_data(G_OBJECT(tree_view), "browser-data");
        gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
        save_bookmarks(GTK_LIST_STORE(model), browser_data->bookmarks_path);
    }
}

// Navigate to selected bookmark
static void navigate_to_bookmark(GtkTreeView *tree_view, GtkTreePath *path,
                               GtkTreeViewColumn *column, gpointer data) {
    BrowserData *browser_data = (BrowserData *)data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gchar *url;
        gtk_tree_model_get(model, &iter, 1, &url, -1);
        
        if (url && is_valid_url(url)) {
            webkit_web_view_load_uri(browser_data->web_view, url);
        }
        
        g_free(url);
    }
}

// Show bookmark manager dialog
static void show_bookmarks(GtkWidget *widget, gpointer data) {
    BrowserData *browser_data = (BrowserData *)data;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Bookmarks",
                                                 GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "Close", GTK_RESPONSE_CLOSE,
                                                 NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 300);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    // Create bookmark view
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser_data->bookmarks_store));
    g_object_set_data(G_OBJECT(tree_view), "browser-data", browser_data);
    
    // Add columns
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree_view),
                                             -1, "Title", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree_view),
                                             -1, "URL", renderer, "text", 1, NULL);
    
    g_signal_connect(tree_view, "row-activated",
                   G_CALLBACK(navigate_to_bookmark), browser_data);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
    
    // Create delete button
    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_END);
    
    GtkWidget *delete_button = gtk_button_new_with_label("Delete");
    g_signal_connect(delete_button, "clicked", G_CALLBACK(delete_bookmark), tree_view);
    gtk_container_add(GTK_CONTAINER(button_box), delete_button);
    
    // Add widgets to dialog
    gtk_box_pack_start(GTK_BOX(content_area), scrolled_window, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content_area), button_box, FALSE, FALSE, 5);
    
    gtk_widget_show_all(content_area);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    // Set environment variables to help with multimedia playback
    // Tell GStreamer to prefer alternative AAC decoders before looking for fdkaac
    setenv("GST_PLUGIN_FEATURE_RANK", "avdec_aac:MAX", 1);
    
    // Optional: For troubleshooting, you can enable more GStreamer debugging
    // setenv("GST_DEBUG", "2", 1);
    
    BrowserData browser_data;
    
    // Initialize GTK
    gtk_init(&argc, &argv);
    
    // Process command line arguments
    const char *home_url = DEFAULT_URL;
    
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--home") == 0 || strcmp(argv[i], "-h") == 0) && i + 1 < argc) {
            // Validate home URL
            if (is_valid_url(argv[i + 1])) {
                home_url = argv[i + 1];
            } else {
                g_print("Warning: Invalid home URL provided, using default\n");
            }
            i++; // Skip the next argument
        } else if (strstr(argv[i], "://") != NULL) {
            // Validate URL
            if (is_valid_url(argv[i])) {
                home_url = argv[i];
            } else {
                g_print("Warning: Invalid URL provided, using default\n");
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            g_print("TinyWeb Usage:\n");
            g_print("  tinyweb [URL]\n");
            g_print("  tinyweb --home URL\n");
            g_print("  tinyweb -h URL\n");
            return 0;
        }
    }
    
    // Setup bookmarks directory and file
    browser_data.bookmarks_path = get_bookmarks_path();
    if (!browser_data.bookmarks_path) {
        g_print("Warning: Could not create bookmarks directory, using temporary storage\n");
    }
    
    // Create the bookmarks store
    browser_data.bookmarks_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    load_bookmarks(browser_data.bookmarks_store, browser_data.bookmarks_path);
    
    // Create a top-level window
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "TinyWeb - by Steve");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), &browser_data);
    
    // Create main vertical layout
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    // Create toolbar
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 5);
    
    // Back button
    GtkWidget *back_button = gtk_button_new_with_label("←");
    gtk_widget_set_tooltip_text(back_button, "Back");
    gtk_box_pack_start(GTK_BOX(toolbar), back_button, FALSE, FALSE, 0);
    
    // Forward button
    GtkWidget *forward_button = gtk_button_new_with_label("→");
    gtk_widget_set_tooltip_text(forward_button, "Forward");
    gtk_box_pack_start(GTK_BOX(toolbar), forward_button, FALSE, FALSE, 0);
    
    // Reload button
    GtkWidget *reload_button = gtk_button_new_with_label("⟳");
    gtk_widget_set_tooltip_text(reload_button, "Reload");
    gtk_box_pack_start(GTK_BOX(toolbar), reload_button, FALSE, FALSE, 0);
    
    // Home button
    GtkWidget *home_button = gtk_button_new_with_label("🏠");
    gtk_widget_set_tooltip_text(home_button, "Home");
    g_object_set_data_full(G_OBJECT(home_button), "home-url", g_strdup(home_url), g_free);
    gtk_box_pack_start(GTK_BOX(toolbar), home_button, FALSE, FALSE, 0);
    
    // URL entry
    GtkWidget *url_entry = gtk_entry_new();
    browser_data.url_entry = url_entry;
    g_signal_connect(url_entry, "activate", G_CALLBACK(navigate_to_url), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), url_entry, TRUE, TRUE, 0);
    
    // Go button
    GtkWidget *go_button = gtk_button_new_with_label("Go");
    gtk_widget_set_tooltip_text(go_button, "Navigate to URL");
    g_signal_connect(go_button, "clicked", G_CALLBACK(navigate_to_url), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), go_button, FALSE, FALSE, 0);
    
    // Add bookmark button
    GtkWidget *add_bookmark_button = gtk_button_new_with_label("⭐");
    gtk_widget_set_tooltip_text(add_bookmark_button, "Add bookmark");
    g_signal_connect(add_bookmark_button, "clicked", G_CALLBACK(add_bookmark), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), add_bookmark_button, FALSE, FALSE, 0);
    
    // Bookmarks button
    GtkWidget *bookmarks_button = gtk_button_new_with_label("📚");
    gtk_widget_set_tooltip_text(bookmarks_button, "Show bookmarks");
    g_signal_connect(bookmarks_button, "clicked", G_CALLBACK(show_bookmarks), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), bookmarks_button, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    
    // Configure WebKit settings for media playback
    WebKitSettings *settings = webkit_settings_new();
    
    // Enable HTML5 media features
    webkit_settings_set_enable_html5_database(settings, TRUE);
    webkit_settings_set_enable_html5_local_storage(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, TRUE);
    webkit_settings_set_enable_mediasource(settings, TRUE);
    webkit_settings_set_media_playback_requires_user_gesture(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    
    // Common browser features
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_javascript_can_access_clipboard(settings, TRUE);
    
    // Set a mainstream user agent for site compatibility
    webkit_settings_set_user_agent(settings, 
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/100.0.4896.127 Safari/537.36");
    
    // Create a WebKit WebView widget with secure settings
    browser_data.web_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_settings(settings));
    
    // Configure web context for caching and cookies
    WebKitWebContext *context = webkit_web_context_get_default();
    webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_WEB_BROWSER);
    
    // Connect navigation button callbacks
    g_signal_connect(back_button, "clicked", G_CALLBACK(go_back), browser_data.web_view);
    g_signal_connect(forward_button, "clicked", G_CALLBACK(go_forward), browser_data.web_view);
    g_signal_connect(reload_button, "clicked", G_CALLBACK(refresh_page), browser_data.web_view);
    g_signal_connect(home_button, "clicked", G_CALLBACK(go_home), browser_data.web_view);
    
    // Handle TLS errors
    g_signal_connect(browser_data.web_view, "load-failed-with-tls-errors", 
                    G_CALLBACK(on_load_failed_with_tls_errors), NULL);
    
    // Connect load changed signal
    g_signal_connect(browser_data.web_view, "load-changed", 
                    G_CALLBACK(web_view_load_changed), &browser_data);
    
    // Create WebView container
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(browser_data.web_view));
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);
    
    // Add a status bar for security information
    GtkWidget *status_bar = gtk_statusbar_new();
    gtk_box_pack_end(GTK_BOX(vbox), status_bar, FALSE, FALSE, 0);
    
    // Load the specified URL after validating
    if (is_valid_url(home_url)) {
        webkit_web_view_load_uri(browser_data.web_view, home_url);
    } else {
        webkit_web_view_load_uri(browser_data.web_view, DEFAULT_URL);
    }
    
    // Show all widgets
    gtk_widget_show_all(window);
    
    // Run the GTK main loop
    gtk_main();
    
    return 0;
}
