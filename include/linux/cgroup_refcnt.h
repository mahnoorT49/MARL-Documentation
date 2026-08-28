/**
 * css_get - obtain a reference on the specified css
 * @css: target css
 *
 * The caller must already have a reference.
 * 
 * 
 * Function Description: Increases the reference count of a cgroup subsystem state. It checks if the CSS has the no-reference flag set; if not, it increments the reference counter. The caller must already hold a reference before calling this function.
 */
CGROUP_REF_FN_ATTRS
void css_get(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_get(&css->refcnt);
}
CGROUP_REF_EXPORT(css_get)

/**
 * css_get_many - obtain references on the specified css
 * @css: target css
 * @n: number of references to get
 *
 * The caller must already have a reference.
 * 
 * 
 * Function Description: Increases the reference count of a cgroup subsystem state by multiple counts. It checks if the CSS has the no-reference flag set; if not, it increments the reference counter by the specified number. The caller must already hold a reference before calling this function.
 */
CGROUP_REF_FN_ATTRS
void css_get_many(struct cgroup_subsys_state *css, unsigned int n)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_get_many(&css->refcnt, n);
}
CGROUP_REF_EXPORT(css_get_many)

/**
 * css_tryget - try to obtain a reference on the specified css
 * @css: target css
 *
 * Obtain a reference on @css unless it already has reached zero and is
 * being released.  This function doesn't care whether @css is on or
 * offline.  The caller naturally needs to ensure that @css is accessible
 * but doesn't have to be holding a reference on it - IOW, RCU protected
 * access is good enough for this function.  Returns %true if a reference
 * count was successfully obtained; %false otherwise.
 * 
 * 
 * Function Description: Tries to increase the reference count of a cgroup subsystem state. It attempts to get a reference only if the CSS has not reached zero and is not being released. Returns true if the reference was obtained, false otherwise. This can be called without holding a reference as long as the CSS is accessed under RCU protection.
 */
CGROUP_REF_FN_ATTRS
bool css_tryget(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		return percpu_ref_tryget(&css->refcnt);
	return true;
}
CGROUP_REF_EXPORT(css_tryget)

/**
 * css_tryget_online - try to obtain a reference on the specified css if online
 * @css: target css
 *
 * Obtain a reference on @css if it's online.  The caller naturally needs
 * to ensure that @css is accessible but doesn't have to be holding a
 * reference on it - IOW, RCU protected access is good enough for this
 * function.  Returns %true if a reference count was successfully obtained;
 * %false otherwise.
 * 
 * 
 * Function Description: Tries to increase the reference count of a cgroup subsystem state only if it is online. It attempts to get a reference from a live CSS that is currently active. Returns true if the reference was obtained, false otherwise. This can be called without holding a reference as long as the CSS is accessed under RCU protection.
 */
CGROUP_REF_FN_ATTRS
bool css_tryget_online(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		return percpu_ref_tryget_live(&css->refcnt);
	return true;
}
CGROUP_REF_EXPORT(css_tryget_online)

/**
 * css_put - put a css reference
 * @css: target css
 *
 * Put a reference obtained via css_get() and css_tryget_online().
 * 
 * 
 * Function Description: Decreases the reference count of a cgroup subsystem state. It checks if the CSS has the no-reference flag set; if not, it decrements the reference counter. This is used to release a reference that was obtained via css_get() or css_tryget_online().
 */
CGROUP_REF_FN_ATTRS
void css_put(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_put(&css->refcnt);
}
CGROUP_REF_EXPORT(css_put)

/**
 * css_put_many - put css references
 * @css: target css
 * @n: number of references to put
 *
 * Put references obtained via css_get() and css_tryget_online().
 * 
 * 
 * Function Description: Decreases the reference count of a cgroup subsystem state by multiple counts. It checks if the CSS has the no-reference flag set; if not, it decrements the reference counter by the specified number. This is used to release multiple references at once.
 */
CGROUP_REF_FN_ATTRS
void css_put_many(struct cgroup_subsys_state *css, unsigned int n)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_put_many(&css->refcnt, n);
}
CGROUP_REF_EXPORT(css_put_many)
