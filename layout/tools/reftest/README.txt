Layout Engine Visual Tests (reftest)
L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
July 19, 2006

This code is designed to run tests of Mozilla's layout engine.  These
tests consist of an HTML (or other format) file along with a reference
in the same format.  The tests are run based on a manifest file, and for
each test, PASS or FAIL is reported, and UNEXPECTED is reported if the
result (PASS or FAIL) was not the expected result noted in the manifest.

Why this way?
=============

Writing HTML tests where the reference rendering is also in HTML is
harder than simply writing bits of HTML that can be regression-tested by
comparing the rendering of an older build to that of a newer build
(perhaps using stored reference images from the older build).  However,
comparing across time has major disadvantages:

 * Comparisons across time either require two runs for every test, or
   they require stored reference images appropriate for the platform and
   configuration (often limiting testing to a very specific
   configuration).

 * Comparisons across time may fail due to expected changes, for
   example, changes in the default style sheet for HTML, changes in the
   appearance of form controls, or changes in default preferences like
   default font size or default colors.

Using tests for which the pass criteria were explicitly chosen allows
running tests at any time to see whether they still pass.

Manifest Format
===============

The test manifest format is a plain text file.  The "#" makes the
remainder of a line a comment.  Each non-blank line (after removal of
comments) must be one of the following:

1. Inclusion of another manifest

   include <relative_path>

2. A test item

   <failure-type>* <type> <url> <url_ref>

   where

   a. <failure-type> (optional) is one of the following:

      fails  The test passes if the images of the two renderings DO NOT
             meet the conditions specified in the <type>.

      fails-if(condition) If the condition is met, the test passes if the 
                          images of the two renderings DO NOT meet the 
                          conditions of <type>. If the condition is not met,
                          the test passes if the conditions of <type> are met.

      random  The results of the test are random and therefore not to be
              considered in the output.

      random-if(condition) The results of the test are random if a given
                           condition is met.

      Examples of random-if:
          random-if(MOZ_WIDGET_TOOLKIT=="windows")
          random-if(MOZ_WIDGET_TOOLKIT=="cocoa")
          random-if(MOZ_WIDGET_TOOLKIT=="gtk2") ...

   b. <type> is one of the following:

      ==  The test passes if the images of the two renderings are the
          SAME.
      !=  The test passes if the images of the two renderings are 
          DIFFERENT.

   c. <url> is either a relative file path or an absolute URL for the
      test page

   d. <url_ref> is either a relative file path or an absolute URL for
      the reference page

   The only difference between <url> and <url_ref> is that results of
   the test are reported using <url> only.

This test manifest format could be used by other harnesses, such as ones
that do not depend on XUL, or even ones testing other layout engines.

Running Tests
=============

At some point in the future there will hopefully be a cleaner way to do
this.  For now, go to your object directory, and run (perhaps using
MOZ_NO_REMOTE=1 or the -profile <directory> option)

./firefox -reftest /path/to/srcdir/mozilla/layout/reftests/reftest.list > reftest.out

and then search/grep reftest.out for "UNEXPECTED".

There are two scripts provided to convert the reftest.out to HTML.
clean-reftest-output.pl converts reftest.out into simple HTML, stripping
lines from the log that aren't relevant.  reftest-to-html.pl converts
the output into html that makes it easier to visually check for
failures.

Testable Areas
==============

This framework is capable of testing many areas of the layout engine.
It is particularly well-suited to testing dynamic change handling (by
comparison to the static end-result as a reference) and incremental
layout (comparison of a script-interrupted layout to one that was not).
However, it is also possible to write tests for many other things that
can be described in terms of equivalence, for example:

 * CSS cascading could be tested by comparing the result of a
   complicated set of style rules that makes a word green to <span
   style="color:green">word</span>.

 * <canvas> compositing operators could be tested by comparing the
   result of drawing using canvas to a block-level element with the
   desired color as a CSS background-color.

 * CSS counters could be tested by comparing the text output by counters
   with a page containing the text written out

 * complex margin collapsing could be tested by comparing the complex
   case to a case where the margin is written out, or where the margin
   space is created by an element with 'height' and transparent
   background

When it is not possible to test by equivalence, it may be possible to
test by non-equivalence.  For example, testing justification in cases
with more than two words, or more than three different words, is
difficult.  However, it is simple to test that justified text is at
least displayed differently from left-, center-, or right-aligned text.

Writing Tests
=============

When writing tests for this framework, it is important for the test to
depend only on behaviors that are known to be correct and permanent.
For example, tests should not depend on default font sizes, default
margins of the body element, the default style sheet used for HTML, the
default appearance of form controls, or anything else that can be
avoided.

In general, the best way to achieve this is to make the test and the
reference identical in as many aspects as possible.  For example:

  Good test markup:
    <div style="color:green"><table><tr><td><span>green
    </span></td></tr></table></div>

  Good reference markup:
    <div><table><tr><td><span style="color:green">green
    </span></td></tr></table></div>

  BAD reference markup:
    <!-- 3px matches the default cellspacing and cellpadding -->
    <div style="color:green; padding: 3px">green
    </div>

  BAD test markup:
    <!-- span doesn't change the positioning, so skip it -->
    <div style="color:green"><table><tr><td>green
    </td></tr></table></div>

Asynchronous Tests
==================

Normally reftest takes a snapshot of the given markup's rendering right
after the load event fires for content. If your test needs to postpone
the moment the snapshot is taken, it should make sure a class
'reftest-wait' is on the root element by the moment the load event
fires. The easiest way to do this is to put it in the markup, e.g.:
    <html class="reftest-wait">

When your test is ready, you should remove this class from the root
element, for example using this code:
    document.documentElement.className = "";


Note that in layout tests it is often enough to trigger layout using 
    document.body.offsetWidth  // HTML example

When possible, you should use this technique instead of making your
test async.
