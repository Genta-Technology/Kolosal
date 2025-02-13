# Contributing to Kolosal AI

Thank you for your interest in contributing to **Kolosal AI**! We welcome contributions from the community—whether that’s a bug fix, a new feature, improvements to documentation, or even just suggestions. By contributing, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md).

---

## Table of Contents

- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Suggesting Enhancements](#suggesting-enhancements)
- [Getting Started](#getting-started)
- [Pull Request Process](#pull-request-process)
- [Coding Guidelines](#coding-guidelines)
- [Commit Message Guidelines](#commit-message-guidelines)
- [Testing Your Changes](#testing-your-changes)
- [License and Commercial Use](#license-and-commercial-use)
- [Additional Resources](#additional-resources)

---

## How to Contribute

Contributions come in many forms. Here are several ways you can help:

### Reporting Bugs

- **Before you file a bug report**, please search the [issues](https://github.com/Genta-Technology/Kolosal/issues) to see if it has already been reported.
- When opening a bug report, please include:
  - A clear and descriptive title.
  - Steps to reproduce the issue.
  - Expected and actual behavior.
  - Any relevant logs or screenshots.
  - Your system configuration (OS, compiler, dependencies, etc.).
  
*Tip: If you’re not sure whether something is a bug or a feature request, feel free to ask in our [Discord community](https://discord.gg/XDmcWqHmJP).*

### Suggesting Enhancements

- Feature requests are welcome!  
- When suggesting a feature, include a clear explanation of the feature and its potential benefits.
- Describe any potential drawbacks or alternatives you considered.
- Feel free to discuss your ideas in our [Discord](https://discord.gg/XDmcWqHmJP) before opening an issue.

---

## Getting Started

Before contributing, please follow these steps:

1. **Fork the Repository**  
   Click the “Fork” button at the top-right of the repository page to create your own copy.

2. **Clone Your Fork**  
   ```bash
   git clone https://github.com/your-username/Kolosal.git
   cd KolosalAI
   ```

3. **Set Up Upstream**  
   To keep your fork up-to-date, add the original repository as an upstream remote:
   ```bash
   git remote add upstream https://github.com/Genta-Technology/Kolosal.git
   ```
   Then, fetch the latest changes:
   ```bash
   git fetch upstream
   ```

4. **Read the Documentation**  
   Familiarize yourself with our [README.md](README.md) and [build instructions](#how-to-compile) to ensure you understand the project structure, dependencies, and build process.

---

## Pull Request Process

1. **Create a Branch**  
   Create a branch with a descriptive name for your changes:
   ```bash
   git checkout -b feature/short-description
   ```

2. **Make Your Changes**  
   - Follow the [coding guidelines](#coding-guidelines) and maintain consistency with existing code.
   - Include clear commit messages (see [commit message guidelines](#commit-message-guidelines)).
   - Ensure that your changes compile and that any new functionality is properly documented.

3. **Commit and Push Your Changes**  
   ```bash
   git add .
   git commit -m "feat: add new feature for XYZ"  # See commit guidelines below
   git push origin feature/short-description
   ```

4. **Submit a Pull Request**  
   Open a pull request (PR) against the `main` branch of the original repository. In your PR description:
   - Describe the problem and your solution.
   - Reference any related issues (e.g., "Fixes #123").
   - Provide context or screenshots, if applicable.

5. **Review Process**  
   - A maintainer will review your PR and may request changes.
   - Feel free to ask questions or provide additional context in the PR discussion.

---

## Coding Guidelines

- **Language Standard:**  
  We use C++17. Make sure your code is compatible with this standard.
- **Code Style:**  
  Follow the style of existing code. Consistency is key!
  - Use meaningful variable and function names.
  - Keep functions short and focused on a single task.
  - Document non-obvious code sections with comments.
- **Directory Structure:**  
  - Place new features or modules in the appropriate directory (e.g., `source/`, `include/`, or as a new folder if it’s a standalone component).
  - Update or add documentation in the relevant section if you modify functionality.
- **Documentation:**  
  Update README and inline comments where necessary.
- **Testing:**  
  If applicable, include unit tests or instructions on how to verify your changes.

---

## Commit Message Guidelines

Use clear and descriptive commit messages. A suggested format is:

```
<type>(<scope>): <subject>

<body>
```

Where:
- **type**:  
  - `feat`: A new feature
  - `fix`: A bug fix
  - `docs`: Documentation changes
  - `style`: Code style changes (formatting, missing semicolons, etc.)
  - `refactor`: Code refactoring without changing functionality
  - `test`: Adding or fixing tests
  - `chore`: Other changes (build process, auxiliary tools, etc.)
- **scope**: A brief description of the affected area (e.g., `build`, `ui`, `engine`).
- **subject**: A concise description of the changes.

*Example:*

```
feat(ui): add dark mode support to settings panel

This commit introduces dark mode options in the settings. The UI components have been updated, and relevant assets have been added.
```

---

## Testing Your Changes

- **Build Locally**  
  Follow the [compiling instructions](#how-to-compile) in the README to ensure your changes build correctly.
- **Run the Application**  
  Verify that your changes work as expected by running the application.
- **Automated Tests**  
  If you add tests:
  - Include instructions for running them.
  - Ensure that tests pass on your local environment.
- **Manual Testing**  
  In case automated tests are not available, provide clear instructions on how to manually verify your changes.

---

## License and Commercial Use

Please note:
- **Kolosal AI** is distributed under the [Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0).  
- The **Genta Inference Engine Personal** has its own licensing restrictions and is distributed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).  
- If you plan to contribute changes that affect the inference engine or its integration, please ensure you are in compliance with the licensing terms.
- For commercial use inquiries, reach out to [rifky@genta.tech](mailto:rifky@genta.tech) or consider contributing an alternative engine as per the project guidelines.

---

## Additional Resources

- [README.md](README.md) – Overview of the project.
- [Build Instructions](#how-to-compile) – Detailed instructions on compiling and running Kolosal AI.
- [Discord Community](https://discord.gg/XDmcWqHmJP) – Join for real-time discussion and support.
- [Issues Tracker](https://github.com/Genta-Technology/Kolosal/issues) – Report bugs and request features.

---

Thank you for contributing to **Kolosal AI**! Your help makes this project better for everyone.

Happy coding!
