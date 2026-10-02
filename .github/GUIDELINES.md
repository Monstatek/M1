GitHub Repository Guidelines
1. Branch Management
Main/Master branch is protected and requires pull request reviews
Feature branches should follow naming convention: feature/descriptive-name
Bug fix branches should follow: fix/issue-description
Hotfix branches: hotfix/urgent-fix-description
Delete branches after merging

2. Commit Messages
Follow conventional commits format:
feat: for new features
fix: for bug fixes
docs: for documentation changes
style: for formatting changes
refactor: for code restructuring
test: for adding tests
chore: for maintenance tasks
Write clear, descriptive messages in present tense
Reference issue numbers when applicable: "feat: add user authentication (#123)"

3. Pull Requests
Create detailed PR descriptions using the provided template
Include:
What changes were made
Why changes were needed
How to test the changes
Screenshots/videos for UI changes
Require at least one approval before merging
Link related issues
Keep PRs focused and reasonably sized

4. Code Review
Review PRs within 24 business hours
Be constructive and respectful in comments
Use GitHub's suggestion feature for small changes
Check for:
Code quality and standards
Test coverage
Documentation
Performance implications
Security concerns

5. CI/CD
All PRs must pass automated tests
Maintain test coverage above agreed threshold (e.g., 80%)
Automated linting must pass
Build must succeed
No security vulnerabilities in dependencies

6. Documentation
Keep README.md updated
Document setup instructions
Maintain API documentation
Update changelog
Comment complex code sections
Include JSDoc/documentation comments for public APIs

7. Issue Management
Use issue templates for:
Bug reports
Feature requests
Documentation updates
Label issues appropriately
Use project boards for tracking
Regular backlog grooming

8. Security
No secrets/credentials in repository
Use environment variables for sensitive data
Regular dependency updates
Security scanning enabled
Follow security best practices

9. Version Control
Follow semantic versioning (MAJOR.MINOR.PATCH)
Maintain a changelog
Tag releases appropriately
Create release notes for significant versions

10. Code Standards
Follow agreed coding style guide
Use consistent formatting (preferably automated)
Write self-documenting code
Follow DRY (Don't Repeat Yourself) principles
Write unit tests for new features

11. Repository Hygiene
Regular dependency updates
Periodic cleanup of old branches
Archive unused repositories
Maintain reasonable repository size
Regular backup procedures

12. Communication
Use GitHub Discussions for technical discussions
Keep relevant conversations in PR comments
Use issue comments for status updates
Tag relevant team members when needed