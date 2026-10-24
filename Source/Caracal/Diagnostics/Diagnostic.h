#pragma once

#include <Caracal/API.h>
#include <Caracal/Diagnostics/DiagnosticKind.h>
#include <Caracal/Diagnostics/DiagnosticLevel.h>
#include <Caracal/Text/SourceLocation.h>
#include <Caracal/Text/SourceText.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Caracal
{
    class CARACAL_API Diagnostic
    {
    public:
        struct Label
        {
            SourceTextSharedPtr source;
            SourceLocation location;
            std::string text;
            bool isPrimary = false;
        };

        struct RelatedReport
        {
            SourceTextSharedPtr source;
            std::string message;
            std::vector<Label> labels;
        };

        Diagnostic(
            DiagnosticLevel level,
            DiagnosticKind kind,
            const SourceTextSharedPtr& source,
            const SourceLocation& location,
            std::optional<std::string> fix = std::nullopt);

        Diagnostic(
            DiagnosticLevel level,
            DiagnosticKind kind,
            std::optional<std::string> fix);

        [[nodiscard]] DiagnosticLevel level() const noexcept { return m_level; }
        [[nodiscard]] DiagnosticKind kind() const noexcept { return m_kind; }
        [[nodiscard]] std::string_view code() const noexcept;
        [[nodiscard]] const std::string& message() const noexcept { return m_message; }
        [[nodiscard]] const SourceTextSharedPtr& source() const noexcept { return m_source; }
        [[nodiscard]] const SourceLocation& location() const noexcept { return m_location; }
        [[nodiscard]] const std::vector<Label>& labels() const noexcept { return m_labels; }
        [[nodiscard]] const std::vector<RelatedReport>& related() const noexcept { return m_related; }
        [[nodiscard]] const std::optional<std::string>& fix() const noexcept { return m_fix; }

        void addPrimaryLabel(const SourceLocation& location, std::string text);
        void addSecondaryLabel(const SourceLocation& location, std::string text);
        void addRelatedPrimaryLabel(const SourceTextSharedPtr& source, const SourceLocation& location, std::string message, std::string text);

    private:
        DiagnosticLevel m_level = DiagnosticLevel::Unknown;
        DiagnosticKind m_kind = DiagnosticKind::Unknown;
        std::string m_message;
        SourceTextSharedPtr m_source;
        SourceLocation m_location;
        std::vector<Label> m_labels;
        std::vector<RelatedReport> m_related;
        std::optional<std::string> m_fix;
    };
}
