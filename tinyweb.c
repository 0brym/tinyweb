#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_URL "https://x.com"
#define BOOKMARKS_FILE "tinyweb_bookmarks.txt"

typedef struct {
    WebKitWebView *web_view;
    GtkWidget *url_entry;
    GtkListStore *bookmarks_store;
} BrowserData;

// Callback to close the application
static void on_destroy(GtkWidget *widget, gpointer data) {
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
    webkit_web_view_load_uri(web_view, home_url);
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
static void load_bookmarks(GtkListStore *store) {
    FILE *file = fopen(BOOKMARKS_FILE, "r");
    if (!file) return;
    
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Split into title and URL
        char *url = strchr(line, '|');
        if (url) {
            *url = '\0';
            url++;
            
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, line, 1, url, -1);
        }
    }
    
    fclose(file);
}

// Save bookmarks to file
static void save_bookmarks(GtkListStore *store) {
    FILE *file = fopen(BOOKMARKS_FILE, "w");
    if (!file) {
        g_warning("Failed to save bookmarks");
        return;
    }
    
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    
    while (valid) {
        gchar *title, *url;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &title, 1, &url, -1);
        
        fprintf(file, "%s|%s\n", title, url);
        
        g_free(title);
        g_free(url);
        
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    
    fclose(file);
}

// Add current page to bookmarks
static void add_bookmark(GtkWidget *widget, gpointer data) {
    BrowserData *browser_data = (BrowserData *)data;
    const gchar *uri = webkit_web_view_get_uri(browser_data->web_view);
    const gchar *title = webkit_web_view_get_title(browser_data->web_view);
    
    if (uri) {
        GtkTreeIter iter;
        gtk_list_store_append(browser_data->bookmarks_store, &iter);
        gtk_list_store_set(browser_data->bookmarks_store, &iter,
                         0, title ? title : uri,
                         1, uri,
                         -1);
        
        save_bookmarks(browser_data->bookmarks_store);
    }
}

// Delete selected bookmark
static void delete_bookmark(GtkWidget *widget, gpointer data) {
    GtkTreeView *tree_view = GTK_TREE_VIEW(data);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
        save_bookmarks(GTK_LIST_STORE(model));
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
        
        webkit_web_view_load_uri(browser_data->web_view, url);
        
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
    BrowserData browser_data;
    
    // Initialize GTK
    gtk_init(&argc, &argv);
    
    // Process command line arguments
    const char *home_url = DEFAULT_URL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--home") == 0 || strcmp(argv[i], "-h") == 0) {
            if (i + 1 < argc) {
                home_url = argv[i + 1];
                i++; // Skip the next argument
            }
        } else if (strstr(argv[i], "://") != NULL) {
            // Treat as URL to open
            home_url = argv[i];
        }
    }
    
    // Create the bookmarks store
    browser_data.bookmarks_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    load_bookmarks(browser_data.bookmarks_store);
    
    // Create a top-level window
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "TinyWeb - by Steve");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    
    // Create main vertical layout
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    // Create toolbar
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 5);
    
    // Back button
    GtkWidget *back_button = gtk_button_new_with_label("←");
    g_signal_connect(back_button, "clicked", G_CALLBACK(go_back), NULL);  // Will set WebView later
    gtk_box_pack_start(GTK_BOX(toolbar), back_button, FALSE, FALSE, 0);
    
    // Forward button
    GtkWidget *forward_button = gtk_button_new_with_label("→");
    g_signal_connect(forward_button, "clicked", G_CALLBACK(go_forward), NULL);  // Will set WebView later
    gtk_box_pack_start(GTK_BOX(toolbar), forward_button, FALSE, FALSE, 0);
    
    // Reload button
    GtkWidget *reload_button = gtk_button_new_with_label("⟳");
    g_signal_connect(reload_button, "clicked", G_CALLBACK(refresh_page), NULL);  // Will set WebView later
    gtk_box_pack_start(GTK_BOX(toolbar), reload_button, FALSE, FALSE, 0);
    
    // Home button
    GtkWidget *home_button = gtk_button_new_with_label("🏠");
    g_object_set_data_full(G_OBJECT(home_button), "home-url", g_strdup(home_url), g_free);
    g_signal_connect(home_button, "clicked", G_CALLBACK(go_home), NULL);  // Will set WebView later
    gtk_box_pack_start(GTK_BOX(toolbar), home_button, FALSE, FALSE, 0);
    
    // URL entry
    GtkWidget *url_entry = gtk_entry_new();
    browser_data.url_entry = url_entry;
    g_signal_connect(url_entry, "activate", G_CALLBACK(navigate_to_url), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), url_entry, TRUE, TRUE, 0);
    
    // Go button
    GtkWidget *go_button = gtk_button_new_with_label("Go");
    g_signal_connect(go_button, "clicked", G_CALLBACK(navigate_to_url), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), go_button, FALSE, FALSE, 0);
    
    // Add bookmark button
    GtkWidget *add_bookmark_button = gtk_button_new_with_label("⭐");
    g_signal_connect(add_bookmark_button, "clicked", G_CALLBACK(add_bookmark), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), add_bookmark_button, FALSE, FALSE, 0);
    
    // Bookmarks button
    GtkWidget *bookmarks_button = gtk_button_new_with_label("📚");
    g_signal_connect(bookmarks_button, "clicked", G_CALLBACK(show_bookmarks), &browser_data);
    gtk_box_pack_start(GTK_BOX(toolbar), bookmarks_button, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    
    // Create a WebKit WebView widget
    browser_data.web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    
    // Update the navigation button callbacks with the WebView instance
    g_object_set_data(G_OBJECT(back_button), "user-data", browser_data.web_view);
    g_signal_handlers_disconnect_by_func(back_button, go_back, NULL);
    g_signal_connect(back_button, "clicked", G_CALLBACK(go_back), browser_data.web_view);
    
    g_object_set_data(G_OBJECT(forward_button), "user-data", browser_data.web_view);
    g_signal_handlers_disconnect_by_func(forward_button, go_forward, NULL);
    g_signal_connect(forward_button, "clicked", G_CALLBACK(go_forward), browser_data.web_view);
    
    g_object_set_data(G_OBJECT(reload_button), "user-data", browser_data.web_view);
    g_signal_handlers_disconnect_by_func(reload_button, refresh_page, NULL);
    g_signal_connect(reload_button, "clicked", G_CALLBACK(refresh_page), browser_data.web_view);
    
    g_object_set_data(G_OBJECT(home_button), "user-data", browser_data.web_view);
    g_signal_handlers_disconnect_by_func(home_button, go_home, NULL);
    g_signal_connect(home_button, "clicked", G_CALLBACK(go_home), browser_data.web_view);
    
    // Connect load changed signal
    g_signal_connect(browser_data.web_view, "load-changed", 
                    G_CALLBACK(web_view_load_changed), &browser_data);
    
    // Create WebView container
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(browser_data.web_view));
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);
    
    // Load the specified URL
    webkit_web_view_load_uri(browser_data.web_view, home_url);
    
    // Show all widgets
    gtk_widget_show_all(window);
    
    // Run the GTK main loop
    gtk_main();
    
    return 0;
}
