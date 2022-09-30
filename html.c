/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 grunfink - MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_regex.h"
#include "xs_set.h"
#include "xs_openssl.h"

#include "snac.h"

d_char *not_really_markdown(char *content, d_char **f_content)
/* formats a content using some Markdown rules */
{
    d_char *s = NULL;
    int in_pre = 0;
    int in_blq = 0;
    xs *list;
    char *p, *v;
    xs *wrk = xs_str_new(NULL);

    {
        /* split by special markup */
        xs *sm = xs_regex_split(content,
            "(`[^`]+`|\\*\\*?[^\\*]+\\*?\\*|https?:/" "/[^ ]*)");
        int n = 0;

        p = sm;
        while (xs_list_iter(&p, &v)) {
            if ((n & 0x1)) {
                /* markup */
                if (xs_startswith(v, "`")) {
                    xs *s1 = xs_crop(xs_dup(v), 1, -1);
                    xs *s2 = xs_fmt("<code>%s</code>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "**")) {
                    xs *s1 = xs_crop(xs_dup(v), 2, -2);
                    xs *s2 = xs_fmt("<b>%s</b>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "*")) {
                    xs *s1 = xs_crop(xs_dup(v), 1, -1);
                    xs *s2 = xs_fmt("<i>%s</i>", s1);
                    wrk = xs_str_cat(wrk, s2);
                }
                else
                if (xs_startswith(v, "http")) {
                    xs *s1 = xs_fmt("<a href=\"%s\">%s</a>", v, v);
                    wrk = xs_str_cat(wrk, s1);
                }
                else
                    /* what the hell is this */
                    wrk = xs_str_cat(wrk, v);
            }
            else
                /* surrounded text, copy directly */
                wrk = xs_str_cat(wrk, v);

            n++;
        }
    }

    /* now work by lines */
    p = list = xs_split(wrk, "\n");

    s = xs_str_new(NULL);

    while (xs_list_iter(&p, &v)) {
        xs *ss = xs_strip(xs_dup(v));

        if (xs_startswith(ss, "```")) {
            if (!in_pre)
                s = xs_str_cat(s, "<pre>");
            else
                s = xs_str_cat(s, "</pre>");

            in_pre = !in_pre;
            continue;
        }

        if (xs_startswith(ss, ">")) {
            /* delete the > and subsequent spaces */
            ss = xs_strip(xs_crop(ss, 1, 0));

            if (!in_blq) {
                s = xs_str_cat(s, "<blockquote>");
                in_blq = 1;
            }

            s = xs_str_cat(s, ss);
            s = xs_str_cat(s, "<br>");

            continue;
        }

        if (in_blq) {
            s = xs_str_cat(s, "</blockquote>");
            in_blq = 0;
        }

        s = xs_str_cat(s, ss);
        s = xs_str_cat(s, "<br>");
    }

    if (in_blq)
        s = xs_str_cat(s, "</blockquote>");
    if (in_pre)
        s = xs_str_cat(s, "</pre>");

    /* some beauty fixes */
    s = xs_replace_i(s, "</blockquote><br>", "</blockquote>");

    *f_content = s;

    return *f_content;
}


int login(snac *snac, char *headers)
/* tries a login */
{
    int logged_in = 0;
    char *auth = xs_dict_get(headers, "authorization");

    if (auth && xs_startswith(auth, "Basic ")) {
        int sz;
        xs *s1 = xs_crop(xs_dup(auth), 6, 0);
        xs *s2 = xs_base64_dec(s1, &sz);
        xs *l1 = xs_split_n(s2, ":", 1);

        if (xs_list_len(l1) == 2) {
            logged_in = check_password(
                xs_list_get(l1, 0), xs_list_get(l1, 1),
                xs_dict_get(snac->config, "passwd"));
        }
    }

    return logged_in;
}


d_char *html_msg_icon(snac *snac, d_char *os, char *msg)
{
    char *actor_id;
    xs *actor = NULL;

    xs *s = xs_str_new(NULL);

    if ((actor_id = xs_dict_get(msg, "attributedTo")) == NULL)
        actor_id = xs_dict_get(msg, "actor");

    if (actor_id && valid_status(actor_get(snac, actor_id, &actor))) {
        xs *name   = NULL;
        xs *avatar = NULL;
        char *v;

        /* get the name */
        if ((v = xs_dict_get(actor, "name")) == NULL) {
            if ((v = xs_dict_get(actor, "preferredUsername")) == NULL) {
                v = "user";
            }
        }

        name = xs_dup(v);

        /* get the avatar */
        if ((v = xs_dict_get(actor, "icon")) != NULL &&
            (v = xs_dict_get(v, "url")) != NULL) {
            avatar = xs_dup(v);
        }

        if (avatar == NULL)
            avatar = xs_fmt("data:image/png;base64, %s", susie);

        {
            xs *s1 = xs_fmt("<p><img class=\"snac-avatar\" src=\"%s\" alt=\"\"/>\n", avatar);
            s = xs_str_cat(s, s1);
        }

        {
            xs *s1 = xs_fmt("<a href=\"%s\" class=\"p-author h-card snac-author\">%s</a>",
                actor_id, name);
            s = xs_str_cat(s, s1);
        }

        if (strcmp(xs_dict_get(msg, "type"), "Note") == 0) {
            xs *s1 = xs_fmt(" <a href=\"%s\">»</a>", xs_dict_get(msg, "id"));
            s = xs_str_cat(s, s1);
        }

        if (!is_msg_public(snac, msg))
            s = xs_str_cat(s, " <span title=\"private\">&#128274;</span>");

        if ((v = xs_dict_get(msg, "published")) == NULL)
            v = "&nbsp;";

        {
            xs *s1 = xs_fmt("<br>\n<time class=\"dt-published snac-pubdate\">%s</time>\n", v);
            s = xs_str_cat(s, s1);
        }
    }

    return xs_str_cat(os, s);
}


d_char *html_user_header(snac *snac, d_char *s, int local)
/* creates the HTML header */
{
    char *p, *v;

    s = xs_str_cat(s, "<!DOCTYPE html>\n<html>\n<head>\n");
    s = xs_str_cat(s, "<meta name=\"viewport\" "
                      "content=\"width=device-width, initial-scale=1\"/>\n");
    s = xs_str_cat(s, "<meta name=\"generator\" "
                      "content=\"" USER_AGENT "\"/>\n");

    /* add server CSS */
    p = xs_dict_get(srv_config, "cssurls");
    while (xs_list_iter(&p, &v)) {
        xs *s1 = xs_fmt("<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\"/>\n", v);
        s = xs_str_cat(s, s1);
    }

    /* add the user CSS */
    {
        xs *css = NULL;
        int size;

        if (valid_status(static_get(snac, "style.css", &css, &size))) {
            xs *s1 = xs_fmt("<style>%s</style>\n", css);
            s = xs_str_cat(s, s1);
        }
    }

    {
        xs *s1 = xs_fmt("<title>%s</title>\n", xs_dict_get(snac->config, "name"));
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</head>\n<body>\n");

    /* top nav */
    s = xs_str_cat(s, "<nav class=\"snac-top-nav\">");

    {
        xs *s1;

        if (local)
            s1 = xs_fmt("<a href=\"%s/admin\">%s</a></nav>\n", snac->actor, L("admin"));
        else
            s1 = xs_fmt("<a href=\"%s\">%s</a></nav>\n", snac->actor, L("public"));

        s = xs_str_cat(s, s1);
    }

    /* user info */
    {
        xs *bio = NULL;
        char *_tmpl =
            "<div class=\"h-card snac-top-user\">\n"
            "<p class=\"p-name snac-top-user-name\">%s</p>\n"
            "<p class=\"snac-top-user-id\">@%s@%s</p>\n"
            "<div class=\"p-note snac-top-user-bio\">%s</div>\n"
            "</div>\n";

        not_really_markdown(xs_dict_get(snac->config, "bio"), &bio);

        xs *s1 = xs_fmt(_tmpl,
            xs_dict_get(snac->config, "name"),
            xs_dict_get(snac->config, "uid"), xs_dict_get(srv_config, "host"),
            bio
        );

        s = xs_str_cat(s, s1);
    }

    return s;
}


d_char *html_top_controls(snac *snac, d_char *s)
/* generates the top controls */
{
    char *_tmpl =
        "<div class=\"snac-top-controls\">\n"

        "<div class=\"snac-note\">\n"
        "<form method=\"post\" action=\"%s/admin/note\">\n"
        "<textarea class=\"snac-textarea\" name=\"content\" "
        "rows=\"8\" wrap=\"virtual\" required=\"required\"></textarea>\n"
        "<input type=\"hidden\" name=\"in_reply_to\" value=\"\">\n"
        "<input type=\"submit\" class=\"button\" value=\"%s\">\n"
        "</form><p>\n"
        "</div>\n"

        "<div class=\"snac-top-controls-more\">\n"
        "<details><summary>%s</summary>\n"

        "<form method=\"post\" action=\"%s/admin/action\">\n"
        "<input type=\"text\" name=\"actor\" required=\"required\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form></p>\n"

        "<form method=\"post\" action=\"%s\">\n"
        "<input type=\"text\" name=\"id\" required=\"required\">\n"
        "<input type=\"submit\" name=\"action\" value=\"%s\"> %s\n"
        "</form></p>\n"

        "<details><summary>%s</summary>\n"

        "<div class=\"snac-user-setup\">\n"
        "<form method=\"post\" action=\"%s/admin/user-setup\">\n"
        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"name\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"text\" name=\"avatar\" value=\"%s\"></p>\n"

        "<p>%s:<br>\n"
        "<textarea name=\"bio\" cols=60 rows=4>%s</textarea></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"password\" name=\"passwd1\" value=\"\"></p>\n"

        "<p>%s:<br>\n"
        "<input type=\"password\" name=\"passwd2\" value=\"\"></p>\n"

        "<input type=\"submit\" class=\"button\" value=\"%s\">\n"
        "</form>\n"

        "</div>\n"
        "</details>\n"
        "</details>\n"
        "</div>\n"
        "</div>\n";

    xs *s1 = xs_fmt(_tmpl,
        snac->actor,
        L("Post"),

        L("More options..."),

        snac->actor,
        L("Follow"), L("(by URL or user@host)"),

        snac->actor,
        L("Boost"), L("(by URL)"),

        L("User setup..."),
        snac->actor,
        L("User name"),
        xs_dict_get(snac->config, "name"),
        L("Avatar URL"),
        xs_dict_get(snac->config, "avatar"),
        L("Bio"),
        xs_dict_get(snac->config, "bio"),
        L("Password (only to change it)"),
        L("Repeat Password"),
        L("Update user info")
    );

    s = xs_str_cat(s, s1);

    return s;
}


d_char *html_button(d_char *s, char *clss, char *label)
{
    xs *s1 = xs_fmt(
               "<input type=\"submit\" name=\"action\" "
               "class=\"snac-btn-%s\" value=\"%s\">\n",
                clss, label);

    return xs_str_cat(s, s1);
}


d_char *html_entry_controls(snac *snac, d_char *os, char *msg)
{
    char *id    = xs_dict_get(msg, "id");
    char *actor = xs_dict_get(msg, "attributedTo");
    char *meta  = xs_dict_get(msg, "_snac");

    xs *s   = xs_str_new(NULL);
    xs *md5 = xs_md5_hex(id, strlen(id));

    s = xs_str_cat(s, "<div class=\"snac-controls\">\n");

    {
        xs *s1 = xs_fmt(
            "<form method=\"post\" action=\"%s/admin/action\">\n"
            "<input type=\"hidden\" name=\"id\" value=\"%s\">\n"
            "<input type=\"hidden\" name=\"actor\" value=\"%s\">\n"
            "<input type=\"button\" name=\"action\" "
            "value=\"%s\" onclick=\""
                "x = document.getElementById('%s_reply'); "
                "if (x.style.display == 'block') "
                "   x.style.display = 'none'; else "
                "   x.style.display = 'block';"
            "\">\n",

            snac->actor, id, actor,
            L("Reply"),
            md5
        );

        s = xs_str_cat(s, s1);
    }

    if (strcmp(actor, snac->actor) != 0) {
        /* controls for other actors than this one */
        char *l;

        l = xs_dict_get(meta, "liked_by");
        if (xs_list_in(l, snac->actor) == -1) {
            /* not already liked; add button */
            s = html_button(s, "like", L("Like"));
        }

        l = xs_dict_get(meta, "announced_by");
        if (xs_list_in(l, snac->actor) == -1) {
            /* not already boosted; add button */
            s = html_button(s, "boost", L("Boost"));
        }

        if (following_check(snac, actor)) {
            s = html_button(s, "unfollow", L("Unfollow"));
        }
        else {
            s = html_button(s, "follow", L("Follow"));
            s = html_button(s, "mute", L("MUTE"));
        }
    }

    s = html_button(s, "delete", L("Delete"));

    s = xs_str_cat(s, "</form>\n");

    {
        /* the post textarea */
        xs *ct = xs_str_new("");

        xs *s1 = xs_fmt(
            "<p><div class=\"snac-note\" style=\"display: none\" id=\"%s_reply\">\n"
            "<form method=\"post\" action=\"%s/admin/note\" id=\"%s_reply_form\">\n"
            "<textarea class=\"snac-textarea\" name=\"content\" "
            "rows=\"4\" wrap=\"virtual\" required=\"required\">%s</textarea>\n"
            "<input type=\"hidden\" name=\"in_reply_to\" value=\"%s\">\n"
            "<input type=\"submit\" class=\"button\" value=\"%s\">\n"
            "</form><p></div>\n",

            md5,
            snac->actor, md5,
            ct,
            id,
            L("Post")
        );

        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</div>\n");

    return xs_str_cat(os, s);
}


d_char *html_entry(snac *snac, d_char *os, char *msg, xs_set *seen, int local, int level)
{
    char *id    = xs_dict_get(msg, "id");
    char *type  = xs_dict_get(msg, "type");
    char *meta  = xs_dict_get(msg, "_snac");
    xs *actor_o = NULL;
    char *actor;

    /* return if already seen */
    if (xs_set_add(seen, id) == 0)
        return os;

    if (strcmp(type, "Follow") == 0)
        return os;

    /* bring the main actor */
    if ((actor = xs_dict_get(msg, "attributedTo")) == NULL)
        return os;

    if (!valid_status(actor_get(snac, actor, &actor_o)))
        return os;

    xs *s = xs_str_new(NULL);

    /* if this is our post, add the score */
    if (xs_startswith(id, snac->actor)) {
        int likes  = xs_list_len(xs_dict_get(meta, "liked_by"));
        int boosts = xs_list_len(xs_dict_get(meta, "announced_by"));

        xs *s1 = xs_fmt(
            "<div class=\"snac-score\">%d &#9733; %d &#8634;</div>\n",
            likes, boosts);

        s = xs_str_cat(s, s1);
    }

    if (level == 0) {
        char *referrer;

        s = xs_str_cat(s, "<div class=\"snac-post\">\n");

        /* print the origin of the post, if any */
        if ((referrer = xs_dict_get(meta, "referrer")) != NULL) {
            xs *actor_r = NULL;

            if (valid_status(actor_get(snac, referrer, &actor_r))) {
                char *name;

                if ((name = xs_dict_get(actor_r, "name")) == NULL)
                    name = xs_dict_get(actor_r, "preferredUsername");

                xs *s1 = xs_fmt(
                    "<div class=\"snac-origin\">"
                    "<a href=\"%s\">%s</a> %s</div>\n",
                    xs_dict_get(actor_r, "id"),
                    name,
                    L("boosted")
                );

                s = xs_str_cat(s, s1);
            }
        }
    }
    else
        s = xs_str_cat(s, "<div class=\"snac-child\">\n");

    s = html_msg_icon(snac, s, msg);

    /* add the content */
    s = xs_str_cat(s, "<div class=\"e-content snac-content\">\n");

    {
        xs *c = xs_dup(xs_dict_get(msg, "content"));

        /* do some tweaks to the content */
        c = xs_replace_i(c, "\r", "");

        while (xs_endswith(c, "<br><br>"))
            c = xs_crop(c, 0, -4);

        c = xs_replace_i(c, "<br><br>", "<p>");

        if (!xs_startswith(c, "<p>")) {
            xs *s1 = c;
            c = xs_fmt("<p>%s</p>", s1);
        }

        s = xs_str_cat(s, c);
    }

    s = xs_str_cat(s, "\n");

    /* add the attachments */
    char *attach;

    if ((attach = xs_dict_get(msg, "attachment")) != NULL) {
        char *v;
        while (xs_list_iter(&attach, &v)) {
            char *t = xs_dict_get(v, "mediaType");

            if (t && xs_startswith(t, "image/")) {
                char *url  = xs_dict_get(v, "url");
                char *name = xs_dict_get(v, "name");

                if (url != NULL) {
                    xs *s1 = xs_fmt("<p><img src=\"%s\" alt=\"%s\"/></p>\n",
                        url, xs_is_null(name) ? "" : name);

                    s = xs_str_cat(s, s1);
                }
            }
        }
    }

    s = xs_str_cat(s, "</div>\n");

    /** controls **/

    if (!local)
        s = html_entry_controls(snac, s, msg);

    /** children **/

    char *children = xs_dict_get(meta, "children");

    if (xs_list_len(children)) {
        int left = xs_list_len(children);
        char *id;

        s = xs_str_cat(s, "<div class=\"snac-children\">\n");

        if (left > 3)
            s = xs_str_cat(s, "<details><summary>...</summary>\n");

        while (xs_list_iter(&children, &id)) {
            xs *chd = timeline_find(snac, id);

            if (left == 0)
                s = xs_str_cat(s, "</details>\n");

            if (chd != NULL)
                s = html_entry(snac, s, chd, seen, local, level + 1);
            else
                snac_debug(snac, 1, xs_fmt("cannot read from timeline child %s", id));

            left--;
        }

        s = xs_str_cat(s, "</div>\n");
    }

    s = xs_str_cat(s, "</div>\n");

    return xs_str_cat(os, s);
}


d_char *html_user_footer(snac *snac, d_char *s)
{
    xs *s1 = xs_fmt(
        "<div class=\"snac-footer\">\n"
        "<a href=\"%s\">%s</a> - "
        "powered by <abbr title=\"Social Networks Are Crap\">snac</abbr></div>\n",
        srv_baseurl,
        L("about this site")
    );

    return xs_str_cat(s, s1);
}


d_char *html_timeline(snac *snac, char *list, int local)
/* returns the HTML for the timeline */
{
    d_char *s = xs_str_new(NULL);
    xs_set *seen = xs_set_new(4096);
    char *v;
    double t = ftime();

    s = html_user_header(snac, s, local);

    if (!local)
        s = html_top_controls(snac, s);

    s = xs_str_cat(s, "<div class=\"snac-posts\">\n");

    while (xs_list_iter(&list, &v)) {
        xs *msg = timeline_get(snac, v);

        s = html_entry(snac, s, msg, seen, local, 0);
    }

    s = xs_str_cat(s, "</div>\n");

    s = html_user_footer(snac, s);

    {
        xs *s1 = xs_fmt("<!-- %lf seconds -->\n", ftime() - t);
        s = xs_str_cat(s, s1);
    }

    s = xs_str_cat(s, "</body>\n</html>\n");

    xs_set_free(seen);

    return s;
}


int html_get_handler(d_char *req, char *q_path, char **body, int *b_size, char **ctype)
{
    int status = 404;
    snac snac;
    char *uid, *p_path;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    if (p_path == NULL) {
        /* public timeline */
        xs *list = local_list(&snac, 0xfffffff);

        *body   = html_timeline(&snac, list, 1);
        *b_size = strlen(*body);
        status  = 200;
    }
    else
    if (strcmp(p_path, "admin") == 0) {
        /* private timeline */

        if (!login(&snac, req))
            status = 401;
        else {
            xs *list = timeline_list(&snac, 0xfffffff);

            *body   = html_timeline(&snac, list, 0);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "p/")) {
        /* a timeline with just one entry */
        xs *id = xs_fmt("%s/%s", snac.actor, p_path);
        xs *fn = _timeline_find_fn(&snac, id);

        if (fn != NULL) {
            xs *list = xs_list_new();
            list = xs_list_append(list, fn);

            *body   = html_timeline(&snac, list, 1);
            *b_size = strlen(*body);
            status  = 200;
        }
    }
    else
    if (xs_startswith(p_path, "s/")) {
        /* a static file */
    }
    else
    if (xs_startswith(p_path, "h/")) {
        /* an entry from the history */
    }
    else
        status = 404;

    user_free(&snac);

    if (valid_status(status)) {
        *ctype = "text/html; charset=utf-8";
    }

    return status;
}


int html_post_handler(d_char *req, char *q_path, d_char *payload, int p_size,
                      char **body, int *b_size, char **ctype)
{
    int status = 0;
    snac snac;
    char *uid, *p_path;
    char *p_vars;

    xs *l = xs_split_n(q_path, "/", 2);

    uid = xs_list_get(l, 1);
    if (!uid || !user_open(&snac, uid)) {
        /* invalid user */
        srv_log(xs_fmt("html_get_handler bad user %s", uid));
        return 404;
    }

    p_path = xs_list_get(l, 2);

    /* all posts must be authenticated */
    if (!login(&snac, req))
        return 401;

    p_vars = xs_dict_get(req, "p_vars");

    {
        xs *j1 = xs_json_dumps_pp(req, 4);
        printf("%s\n", j1);
        printf("[%s]\n", p_path);
    }

    if (p_path && strcmp(p_path, "admin/note") == 0) {
        /* post note */
        char *content     = xs_dict_get(p_vars, "content");
        char *in_reply_to = xs_dict_get(p_vars, "in_reply_to");

        if (content != NULL) {
            xs *msg   = NULL;
            xs *c_msg = NULL;

            msg = msg_note(&snac, content, NULL, in_reply_to);

            c_msg = msg_create(&snac, msg);

            post(&snac, c_msg);

            timeline_add(&snac, xs_dict_get(msg, "id"), msg, in_reply_to, NULL);
        }

        status = 303;
    }
    else
    if (p_path && strcmp(p_path, "admin/action") == 0) {
        /* action on an entry */
        char *id     = xs_dict_get(p_vars, "id");
        char *actor  = xs_dict_get(p_vars, "actor");
        char *action = xs_dict_get(p_vars, "action");

        if (action == NULL)
            return 404;

        if (strcmp(action, "Like") == 0) {
            xs *msg = msg_admiration(&snac, id, "Like");
            post(&snac, msg);
            timeline_admire(&snac, id, snac.actor, 1);

            status = 303;
        }
        else
        if (strcmp(action, "Boost") == 0) {
            xs *msg = msg_admiration(&snac, id, "Announce");
            post(&snac, msg);
            timeline_admire(&snac, id, snac.actor, 0);

            status = 303;
        }
        else
            status = 404;
    }
    else
    if (p_path && strcmp(p_path, "admin/user-setup") == 0) {
        /* change of user data */
    }

    if (status == 303) {
        *body   = xs_fmt("%s/admin", snac.actor);
        *b_size = strlen(*body);
    }

    return status;
}

