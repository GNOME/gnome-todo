# Contributing to GNOME To Do

When contributing to the development of GNOME To Do, please first discuss the change
you wish to make via issue, email, or any other method with the maintainers before
making a change.

Please note we have a [Code of Conduct](#code-of-conduct), please follow it in all
your interactions with the project.

# Table of contents
1. [Creating an issue](#issues-issues-and-more-issues)
  1. [Template](#issue-template)
  2. [Example of a good issue](#example-task-rows-should-have-a-maximum-width)
2. [Pull requests](#pull-request-process)
3. [Code of Conduct](#code-of-conduct)

# Issues, issues and more issues!

There are many ways you can contribute to GNOME To Do, and all of them involve creating issues
in [GNOME To Do issue tracker](https://gitlab.gnome.org/GNOME/gnome-todo/issues). This is the
entry point for your contribution.

To create an effective and high quality ticket, try to put the following information on your
ticket:

 1. A detailed description of the issue or feature request
     - For issues, please add the necessary steps to reproduce the issue.
     - For feature requests, add a detailed description of your proposal.
 2. A checklist of Development tasks
 3. A checklist of Design tasks
 4. A checklist of QA tasks

## Issue template
```
[Title of the issue or feature request]

Detailed description of the issue. Put as much information as you can, potentially
with images showing the issue or mockups of the proposed feature.

If it's an issue, add the steps to reproduce like this:

Steps to reproduce:

1. Open GNOME To Do
2. Create a task
3. ...

## Design Tasks

* [ ]  design tasks

## Development Tasks

* [ ]  development tasks

## QA Tasks

* [ ]  qa (quality assurance) tasks
```


This is an example of a good and informative issue:

---
<pre>
<h1>Example: Task rows should have a maximum width</h1>

When using a very wide window, task rows grow super wide currently, because they
always take up the entire width of the window. This is not great for window sizes
larger than about 600px.

<img src="https://gitlab.gnome.org/GNOME/gnome-todo/uploads/a414239a44c5b2714634df5cb85a7a78/too-wide.png" width="400px" />

This is how it would look with a maximum width of 650px:

<img src="https://gitlab.gnome.org/GNOME/gnome-todo/uploads/7dfc77b1c3cc942cf1977770ea15b198/too-wide-fixed.png" width="400px" />

<h3>Design Tasks</h3>
* [x] Define how much rows should expand horizontally

<h3>Development Tasks</h3>
* [ ] Implement maximum-width rows

<h3>QA Tasks</h3>
* [ ] Rows don't grow horizontally above 650px
* [ ] No regressions were introduced

</pre>

---

# Pull Request Process

1. Ensure your code compiles. Run `make` before creating the pull request.
2. If you're adding new external API, it must be properly documented.
3. The commit message is formatted as follows:

```
   component: <summary>

   A paragraph explaining the problem and its context.

   Another one explaining how you solved that.

   <link to the bug ticket>
```

4. You may merge the pull request in once you have the sign-off of the maintainers, or if you
   do not have permission to do that, you may request the second reviewer to merge it for you.

---

# Code of Conduct

GNOME To Do is a project developed based on GNOME Code of Conduct. You can read it below:

## Summary

GNOME creates software for a better world. We achieve this by behaving well towards
each other.

Therefore this document suggests what we consider ideal behaviour, so you know what
to expect when getting involved in GNOME. This is who we are and what we want to be.
There is no official enforcement of these principles, and this should not be interpreted
like a legal document.

## Advice

 * **Be respectful and considerate**: Disagreement is no excuse for poor behaviour or personal
     attacks. Remember that a community where people feel uncomfortable is not a productive one.

 * **Be patient and generous**: If someone asks for help it is because they need it. Do politely
     suggest specific documentation or more appropriate venues where appropriate, but avoid
     aggressive or vague responses such as "RTFM".

 * **Assume people mean well**: Remember that decisions are often a difficult choice between
     competing priorities. If you disagree, please do so politely. If something seems outrageous,
     check that you did not misinterpret it. Ask for clarification, but do not assume the worst.

 * **Try to be concise**: Avoid repeating what has been said already. Making a conversation larger
     makes it difficult to follow, and people often feel personally attacked if they receive multiple
     messages telling them the same thing.


In the interest of fostering an open and welcoming environment, we as
contributors and maintainers pledge to making participation in our project and
our community a harassment-free experience for everyone, regardless of age, body
size, disability, ethnicity, gender identity and expression, level of experience,
nationality, personal appearance, race, religion, or sexual identity and
orientation.

---

# Attribution

This Code of Conduct is adapted from the [Contributor Covenant][homepage], version 1.4,
available at [http://contributor-covenant.org/version/1/4][version]

[homepage]: http://contributor-covenant.org
[version]: http://contributor-covenant.org/version/1/4/
