### How iters currently look like

When an album is selected, the track window is initialized with an iter of the following form

    {
        data0 = &album->track_root,
        data1 = NULL,
        data2 = NULL
    }

When `tree_track_get_next_by_album` iterates over this iter, `data1` is set to the current track.
`data2` is always `NULL`.

### How the new iter looks like when an artist is selected

When an artist is selected, the track window is initialized with an iter of the following form

    {
        data0 = &artist->album_root,
        data1 = NULL,
        data2 = NULL
    }

When `tree_track_get_next_by_artist` gets this iter for the first time, both `data1` and `data2` are set to `to_album(rb_first(data0))`.
This is interpreted by `print_track` as the command to draw the album header.
Subsequent calls of `ttgnba` iterate over the tracks and save them in `data1`.
Once all tracks are done, we continue as above but with `rb_next(&album->tree_node)`.

### How the tree iter looks like

When an artist is selected, the tree iter look like this:

    {
        data0 = tree_root,
        data1 = artist,
        data2 = NULL
    }

When an album is selected, the tree iter look like this:

    {
        data0 = tree_root,
        data1 = artist,
        data2 = album
    }

### How the track window calls `ttgnb_album` and `ttgnb_artist`

Since the track window cannot be told which function to use when it is initialized with an iter, it always calls the function `tree_track_get_next`.
In there, we have to distinguish the two cases. 
I don't think it's possible to distingish those cases by looking at the iter itself.
Instead we use the following function:

    static inline int tree_album_selected(void)
    {
        return iter_to_album(&lib_tree_win->sel) != NULL;
    }

where

    static inline struct album *iter_to_album(const struct iter *iter)
    {
        return iter->data2;
    }

and redirect the calls to `ttgnb_album` or `ttgnb_artist` depending on which type is selected in the tree.
This is possible because of the previous paragraph.
