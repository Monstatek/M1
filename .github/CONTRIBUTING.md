# Contributing to M1 NFC Project

Thank you for your interest in contributing to our M1 project! This document provides guidelines and processes for contributing to the project.

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Process](#development-process)
- [How to Submit Changes](#how-to-submit-changes)
- [Coding Standards](#coding-standards)
- [Testing Guidelines](#testing-guidelines)
- [Documentation](#documentation)
- [Issue and Pull Request Process](#issue-and-pull-request-process)

## Code of Conduct

This project and everyone participating in it is governed by our [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

## Getting Started

1. Fork the repository
2. Clone your fork:
   ```bash
   git clone https://github.com/YOUR-USERNAME/m1_01.git
   ```
3. Add the upstream repository:
   ```bash
   git remote add upstream https://github.com/Monstatek/m1_01.git
   ```
4. Create a new branch:
   ```bash
   git checkout -b feature/your-feature-name
   ```

## Development Process

1. **Branch Naming Convention**
   - `feature/` - for new features (e.g., `feature/nfc-reader`)
   - `fix/` - for bug fixes (e.g., `fix/nfc-communication`)
   - `docs/` - for documentation
   - `refactor/` - for code refactoring
   - `test/` - for adding tests

2. **Commit Messages**
   We follow the [Conventional Commits](https://www.conventionalcommits.org/) specification:
   ```
   <type>(<scope>): <description>

   [optional body]

   [optional footer]
   ```
   Types include:
   - feat: new feature
   - fix: bug fix
   - docs: documentation changes
   - style: formatting, missing semicolons, etc.
   - refactor: code restructuring
   - test: adding tests
   - chore: maintenance tasks

## How to Submit Changes

1. Update your fork to the latest upstream version
2. Create your feature branch
3. Make your changes
4. Run tests and ensure they pass
5. Update documentation as needed
6. Commit your changes
7. Push to your fork
8. Create a Pull Request

### Pull Request Template

When creating a pull request, please include:

```markdown
## Description
[Describe your changes here]

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Hardware Requirements
[Specify any hardware requirements or changes]

## Testing Environment
- STM32 Board: [Specify model]
- NFC Module: [Specify model]
- Other peripherals: [List any]

## How Has This Been Tested?
[Describe your testing approach]

## Checklist:
- [ ] My code follows the project's style guidelines
- [ ] I have performed a self-review
- [ ] I have commented my code, particularly in hard-to-understand areas
- [ ] I have updated the documentation
- [ ] My changes generate no new warnings
- [ ] I have added tests that prove my fix/feature works
- [ ] New and existing unit tests pass locally
```

## Coding Standards

- Follow STM32 HAL coding conventions
- Use meaningful variable and function names
- Write self-documenting code
- Keep functions focused and concise
- Add comments for complex logic
- Follow DRY (Don't Repeat Yourself) principles

### C-Specific Guidelines
- Use camelCase for variables and functions
- Use UPPER_CASE for constants and macros
- Use descriptive names for all identifiers
- Comment complex algorithms
- Follow STM32 HAL naming conventions
- Use appropriate data types (uint8_t, uint16_t, etc.)

## Testing Guidelines

- Test on actual hardware when possible
- Document test scenarios
- Include edge cases
- Test NFC communication thoroughly
- Verify power consumption
- Test with different NFC tags

## Documentation

- Update README.md when adding new features
- Start at the [technical documentation index](../docs/README.md)
- Document all public APIs
- Include Doxygen-style comments for functions
- Update changelog
- Add inline comments for complex logic
- Document hardware connections and pin configurations

## Issue and Pull Request Process

### Creating an Issue
- Use the appropriate issue template
- Provide clear reproduction steps for bugs
- Include hardware configuration
- Add relevant labels
- Include logs or error messages

### Pull Request Review Process
1. Automated checks must pass
2. At least one approval required
3. All conversations must be resolved
4. Documentation updated
5. Tests included and passing

## Additional Resources

- [STM32 HAL Documentation](https://www.st.com/en/embedded-software/stm32cube-mcu-mpu-packages.html)
- [M1 build documentation](../documentation/mbt.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [License](../LICENSE)
- [Security Policy](SECURITY.md)

## Questions?

Feel free to contact the maintainers if you have any questions:
- [MonstaTek maintainers](mailto:minujaeger@monstatek.com)

## Recognition

Contributors will be recognized in our [CONTRIBUTORS.md](CONTRIBUTORS.md) file.

---

Remember that this is a guideline, not a rule book. Use your best judgment, and feel free to propose changes to this document in a pull request.

Thank you for contributing! 🎉
