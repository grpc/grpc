# bash completion for cmake(1)                             -*- shell-script -*-

_cmake()
{
    local cur prev words cword split=false
    if type -t _init_completion >/dev/null; then
        _init_completion -n = || return
    else
        # manual initialization for older bash completion versions
        COMPREPLY=()
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
    fi

    # Workaround for options like -DCMAKE_BUILD_TYPE=Release
    local prefix=
    if [[ $cur == -D* ]]; then
        prev=-D
        prefix=-D
        cur="${cur#-D}"
    elif [[ $cur == -U* ]]; then
        prev=-U
        prefix=-U
        cur="${cur#-U}"
    fi

    case "$prev" in
        -D)
            if [[ $cur == *=* ]]; then
            # complete values for variables
                local var type value
                var="${cur%%[:=]*}"
                value="${cur#*=}"

                if [[ $cur == CMAKE_BUILD_TYPE* ]]; then # most widely used case
                    COMPREPLY=( $( compgen -W 'Debug Release RelWithDebInfo
                        MinSizeRel' -- "$value" ) )
                    return
                fi

                if [[ $cur == *:* ]]; then
                    type="${cur#*:}"
                    type="${type%%=*}"
                else # get type from cache if it's not set explicitly
                    type=$( cmake -LA -N 2>/dev/null | grep "$var:" \
                        2>/dev/null )
                    type="${type#*:}"
                    type="${type%%=*}"
                fi
                case "$type" in
                    FILEPATH)
                        cur="$value"
                        _filedir
                        return
                        ;;
                    PATH)
                        cur="$value"
                        _filedir -d
                        return
                        ;;
                    BOOL)
                        COMPREPLY=( $( compgen -W 'ON OFF TRUE FALSE' -- \
                            "$value" ) )
                        return
                        ;;
                    STRING|INTERNAL)
                        # no completion available
                        return
                        ;;
                esac
            elif [[ $cur == *:* ]]; then
            # complete types
                local type="${cur#*:}"
                COMPREPLY=( $( compgen -W 'FILEPATH PATH STRING BOOL INTERNAL'\
                    -S = -- "$type" ) )
                compopt -o nospace
            else
            # complete variable names
                COMPREPLY=( $( compgen -W '$( cmake -LA -N 2>/dev/null |
                    tail -n +2 | cut -f1 -d: )' -P "$prefix" -- "$cur" ) )
                compopt -o nospace
            fi
            return
            ;;
        -U)
            COMPREPLY=( $( compgen -W '$( cmake -LA -N | tail -n +2 |
                cut -f1 -d: )' -P "$prefix" -- "$cur" ) )
            return
            ;;
    esac

    _split_longopt && split=true

    case "$prev" in
        -C|-P|--graphviz|--system-information)
            _filedir
            return
            ;;
        --build|--install|--open)
            _filedir -d
            return
            ;;
        -E)
            COMPREPLY=( $( compgen -W "$( cmake -E help |& sed -n \
                '/^  [^ ]/{s|^  \([^ ]\{1,\}\) .*$|\1|;p}' 2>/dev/null )" \
                -- "$cur" ) )
            return
            ;;
        -G)
            local IFS=$'\n'
            local quoted
            printf -v quoted %q "$cur"
            COMPREPLY=( $( compgen -W '$( cmake --help 2>/dev/null | sed -n \
                -e "1,/^Generators/d" \
                -e "/^  *[^ =]/{s|^ *\([^=]*[^ =]\).*$|\1|;s| |\\\\ |g;p}" \
                2>/dev/null )' -- "$quoted" ) )
            return
            ;;
        --loglevel)
            COMPREPLY=( $(compgen -W 'error warning notice status verbose debug trace' -- $cur ) )
            ;;
        --help-command)
            COMPREPLY=( $( compgen -W '$( cmake --help-command-list 2>/dev/null|
                grep -v "^cmake version " )' -- "$cur" ) )
            return
            ;;
        --help-manual)
            COMPREPLY=( $( compgen -W '$( cmake --help-manual-list 2>/dev/null|
                grep -v "^cmake version " | sed -e "s/([0-9])$//" )' -- "$cur" ) )
            return
            ;;
        --help-module)
            COMPREPLY=( $( compgen -W '$( cmake --help-module-list 2>/dev/null|
                grep -v "^cmake version " )' -- "$cur" ) )
            return
            ;;
         --help-policy)
            COMPREPLY=( $( compgen -W '$( cmake --help-policy-list 2>/dev/null |
                grep -v "^cmake version " )' -- "$cur" ) )
            return
            ;;
         --help-property)
            COMPREPLY=( $( compgen -W '$( cmake --help-property-list \
                2>/dev/null | grep -v "^cmake version " )' -- "$cur" ) )
            return
            ;;
         --help-variable)
            COMPREPLY=( $( compgen -W '$( cmake --help-variable-list \
                2>/dev/null | grep -v "^cmake version " )' -- "$cur" ) )
            return
            ;;
    esac

    $split && return

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W '$( _parse_help "$1" --help )' -- ${cur}) )
        [[ $COMPREPLY == *= ]] && compopt -o nospace
        [[ $COMPREPLY ]] && return
    fi

    _filedir
} &&
complete -F _cmake cmake

# ex: ts=4 sw=4 et filetype=sh
