# ESP-IDF Development Container with C++ tooling
# ==============================================
# Extends the official ESP-IDF image with:
# - clangd, clang-format, clang-tidy
# - Powerlevel10k zsh theme
# - Development utilities

FROM espressif/idf:v5.5.2

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# =============================================================================
# Install Development Tools
# =============================================================================
# clang-format/tidy from apt for code formatting
# bear for generating compile_commands.json
# golang for setup tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-format \
    clang-tidy \
    bear \
    git \
    curl \
    zsh \
    golang \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install esp-clang (ESP-IDF's LLVM toolchain with clangd)
# =============================================================================
# esp-clang provides clangd with native RISC-V/Xtensa support for ESP32 targets
RUN . /opt/esp/idf/export.sh \
    && idf_tools.py install esp-clang \
    && idf_tools.py install-python-env

# =============================================================================
# Install Python packages for protobuf generation
# =============================================================================
# grpcio-tools provides protoc compiler
# nanopb provides nanopb_generator for embedded protobuf
RUN . /opt/esp/idf/export.sh \
    && pip install --no-cache-dir grpcio-tools nanopb

# Create stable symlink for esp-clang - find the actual clangd and work backwards
RUN CLANGD_PATH=$(find /opt/esp/tools -name "clangd" -type f 2>/dev/null | head -1) \
    && if [ -n "$CLANGD_PATH" ]; then \
         ESP_CLANG_BIN_DIR=$(dirname "$CLANGD_PATH"); \
         ESP_CLANG_ROOT=$(dirname "$ESP_CLANG_BIN_DIR"); \
         ln -sf "$ESP_CLANG_ROOT" /opt/esp/esp-clang; \
         echo "Created symlink: /opt/esp/esp-clang -> $ESP_CLANG_ROOT"; \
         ls -la /opt/esp/esp-clang/bin/clangd; \
       else \
         echo "ERROR: clangd not found after esp-clang installation!"; \
         exit 1; \
       fi

# =============================================================================
# Setup non-root user (vscode) for devcontainer
# =============================================================================
# No sudo - principle of least privilege. All packages installed above as root.
# Device access (/dev/ttyUSB0) is handled via Docker's --device flag.
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

# Create user, handling case where GID/UID already exists in base image
RUN if getent group $USER_GID > /dev/null 2>&1; then \
      echo "Group $USER_GID exists, using it"; \
    else \
      groupadd --gid $USER_GID $USERNAME; \
    fi \
    && if id -u $USER_UID > /dev/null 2>&1; then \
      echo "User $USER_UID exists, renaming to $USERNAME"; \
      usermod -l $USERNAME -d /home/$USERNAME -m $(id -nu $USER_UID); \
    else \
      useradd --uid $USER_UID --gid $USER_GID -m $USERNAME; \
    fi \
    && usermod -aG dialout $USERNAME

# =============================================================================
# Install Oh My Zsh, Powerlevel10k, and plugins (as vscode user)
# =============================================================================
USER $USERNAME
WORKDIR /home/$USERNAME

# Install Oh My Zsh
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended

# Install Powerlevel10k theme
RUN git clone --depth=1 https://github.com/romkatv/powerlevel10k.git \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/themes/powerlevel10k

# -----------------------------------------------------------------------------
# Install external zsh plugins
# -----------------------------------------------------------------------------
# zsh-autosuggestions: Fish-like suggestions based on command history
RUN git clone --depth=1 https://github.com/zsh-users/zsh-autosuggestions \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/plugins/zsh-autosuggestions

# zsh-syntax-highlighting: Syntax highlighting for commands as you type
RUN git clone --depth=1 https://github.com/zsh-users/zsh-syntax-highlighting \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/plugins/zsh-syntax-highlighting

# zsh-completions: Additional completion definitions
RUN git clone --depth=1 https://github.com/zsh-users/zsh-completions \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/plugins/zsh-completions

# zsh-history-substring-search: Fish-like history search with up/down arrows
RUN git clone --depth=1 https://github.com/zsh-users/zsh-history-substring-search \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/plugins/zsh-history-substring-search

# -----------------------------------------------------------------------------
# Configure zsh
# -----------------------------------------------------------------------------
# Set theme to Powerlevel10k
RUN sed -i 's/^ZSH_THEME=.*/ZSH_THEME="powerlevel10k\/powerlevel10k"/' ~/.zshrc

# Enable plugins (built-in + external)
# Built-in: git, command-not-found, colored-man-pages, extract
# External: zsh-autosuggestions, zsh-syntax-highlighting, zsh-completions, zsh-history-substring-search
RUN sed -i 's/^plugins=(git)/plugins=(\n  git\n  command-not-found\n  colored-man-pages\n  extract\n  zsh-autosuggestions\n  zsh-syntax-highlighting\n  zsh-completions\n  history-substring-search\n)/' ~/.zshrc

# Add instant prompt to .zshrc (at the top)
RUN echo '# Enable Powerlevel10k instant prompt (should stay at the top of ~/.zshrc)\n\
if [[ -r "${XDG_CACHE_HOME:-$HOME/.cache}/p10k-instant-prompt-${(%):-%n}.zsh" ]]; then\n\
  source "${XDG_CACHE_HOME:-$HOME/.cache}/p10k-instant-prompt-${(%):-%n}.zsh"\n\
fi\n' | cat - ~/.zshrc > /tmp/zshrc && mv /tmp/zshrc ~/.zshrc

# Add p10k sourcing, ESP-IDF, and keybindings to .zshrc (at the end)
RUN echo '\n\
# ESP-IDF environment\n\
. /opt/esp/idf/export.sh > /dev/null 2>&1\n\
\n\
# History substring search keybindings (Up/Down arrow)\n\
bindkey "^[[A" history-substring-search-up\n\
bindkey "^[[B" history-substring-search-down\n\
\n\
# Accept autosuggestion with Ctrl+Space or End key\n\
bindkey "^ " autosuggest-accept\n\
bindkey "^[[F" autosuggest-accept\n\
\n\
# To customize prompt, run `p10k configure` or edit ~/.p10k.zsh.\n\
[[ ! -f ~/.p10k.zsh ]] || source ~/.p10k.zsh\n' >> ~/.zshrc

# Create cache directory for p10k instant prompt
RUN mkdir -p /home/$USERNAME/.cache

# =============================================================================
# Set default shell to zsh
# =============================================================================
ENV SHELL=/bin/zsh

# Switch back to root for any remaining setup
USER root

# Ensure vscode user can access ESP-IDF python environment
RUN chown -R 1000:1000 /opt/esp/python_env || true

# Set default user back to vscode
USER $USERNAME
WORKDIR /workspaces

