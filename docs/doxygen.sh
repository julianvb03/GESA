#!/bin/bash

# Script para gestionar documentación Doxygen con Docker - Proyecto GESA
# Autor: Generado para estructura modular C++

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PROJECT_NAME="GESA"
DOCKER_IMAGE="gesa-doxygen"
DOCS_PORT=8080

DOCS_DIR="$SCRIPT_DIR"
DOXYFILE="$DOCS_DIR/Doxyfile"
CUSTOM_DOXYFILE="$DOCS_DIR/Doxyfile.custom"
DOCKERFILE="$DOCS_DIR/Dockerfile.doxygen"
OUTPUT_HTML_DIR="$DOCS_DIR/html"
OUTPUT_LATEX_DIR="$DOCS_DIR/latex"

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_info() {
    echo -e "${YELLOW}ℹ${NC} $1"
}

# Verificar si Docker está instalado
check_docker() {
    if ! command -v docker &> /dev/null; then
        print_error "Docker no está instalado. Por favor instala Docker primero."
        exit 1
    fi
}

# Construir imagen Docker
build_image() {
    print_header "Construyendo imagen Docker"

    if [ ! -f "$DOCKERFILE" ]; then
        print_error "No se encontró $DOCKERFILE"
        exit 1
    fi

    docker build -f "$DOCKERFILE" -t "$DOCKER_IMAGE" "$PROJECT_ROOT"
    print_success "Imagen $DOCKER_IMAGE construida exitosamente"
}

# Generar archivo de configuración base
generate_config() {
    print_header "Generando configuración Doxyfile"

    if [ -f "$DOXYFILE" ]; then
        print_info "Ya existe un Doxyfile en docs. ¿Sobrescribir? (s/N)"
        read -r response
        if [[ ! "$response" =~ ^[Ss]$ ]]; then
            print_info "Operación cancelada"
            exit 0
        fi
    fi

    mkdir -p "$DOCS_DIR"

    if [ -f "$CUSTOM_DOXYFILE" ]; then
        cp "$CUSTOM_DOXYFILE" "$DOXYFILE"
        print_success "Doxyfile personalizado copiado"
    else
        docker run --rm -v "$PROJECT_ROOT:/project" -w /project/docs "$DOCKER_IMAGE" -g Doxyfile
        print_success "Doxyfile base generado en docs/"
        print_info "Edita docs/Doxyfile según tus necesidades antes de generar la documentación"
    fi
}

# Generar documentación
generate_docs() {
    print_header "Generando documentación"

    if [ ! -f "$DOXYFILE" ]; then
        print_error "No se encontró docs/Doxyfile. Ejecuta primero: $0 config"
        exit 1
    fi

    mkdir -p "$DOCS_DIR"

    docker run --rm -v "$PROJECT_ROOT:/project" -w /project/docs "$DOCKER_IMAGE" Doxyfile

    if [ -d "$OUTPUT_HTML_DIR" ]; then
        print_success "Documentación generada en ./docs/html/"
        print_info "Para verla, ejecuta: $0 serve"
    else
        print_error "Error al generar la documentación"
        exit 1
    fi
}

# Servir documentación con nginx
serve_docs() {
    print_header "Sirviendo documentación"

    if [ ! -d "$OUTPUT_HTML_DIR" ]; then
        print_error "No se encontró documentación. Ejecuta primero: $0 generate"
        exit 1
    fi

    print_info "Iniciando servidor web en http://localhost:$DOCS_PORT"

    docker run --rm -d \
        --name gesa-docs-server \
        -p $DOCS_PORT:80 \
        -v "$OUTPUT_HTML_DIR:/usr/share/nginx/html:ro" \
        nginx:alpine

    print_success "Servidor iniciado"
    print_info "Abre tu navegador en: http://localhost:$DOCS_PORT"
    print_info "Para detener el servidor: $0 stop"
}

# Detener servidor
stop_server() {
    print_header "Deteniendo servidor"

    if docker ps | grep -q gesa-docs-server; then
        docker stop gesa-docs-server
        print_success "Servidor detenido"
    else
        print_info "El servidor no está corriendo"
    fi
}

# Limpiar archivos generados
clean() {
    print_header "Limpiando archivos generados"

    print_info "¿Estás seguro de eliminar la documentación generada? (s/N)"
    read -r response

    if [[ "$response" =~ ^[Ss]$ ]]; then
        rm -rf "$OUTPUT_HTML_DIR" "$OUTPUT_LATEX_DIR"
        print_success "Documentación eliminada"
    else
        print_info "Operación cancelada"
    fi
}

# Flujo completo
full_workflow() {
    print_header "Ejecutando flujo completo"

    build_image
    echo ""

    if [ ! -f "$DOXYFILE" ]; then
        generate_config
        echo ""
        print_info "Revisa y edita docs/Doxyfile, luego ejecuta: $0 generate"
        exit 0
    fi

    generate_docs
    echo ""
    serve_docs
}

# Estadísticas de documentación
stats() {
    print_header "Estadísticas de Documentación"

    if [ ! -d "$OUTPUT_HTML_DIR" ]; then
        print_error "No hay documentación generada"
        exit 1
    fi

    echo ""
    echo "📁 Archivos fuente:"
    find "$PROJECT_ROOT/src" "$PROJECT_ROOT/include" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) 2>/dev/null | wc -l || echo "0"

    echo ""
    echo "📄 Páginas HTML generadas:"
    find "$OUTPUT_HTML_DIR" -name "*.html" 2>/dev/null | wc -l || echo "0"

    echo ""
    echo "📊 Tamaño de documentación:"
    du -sh "$DOCS_DIR/" 2>/dev/null || echo "N/A"
}

# Mostrar ayuda
show_help() {
    cat << EOF
${BLUE}Documentación Doxygen para $PROJECT_NAME${NC}

${GREEN}Uso:${NC} $0 [comando]

${GREEN}Comandos disponibles:${NC}

  ${YELLOW}Básicos:${NC}
    build       Construir imagen Docker
    config      Generar/actualizar docs/Doxyfile
    generate    Generar documentación
    serve       Servir documentación en http://localhost:$DOCS_PORT
    stop        Detener servidor web
    
  ${YELLOW}Utilidades:${NC}
    full        Ejecutar flujo completo (build + config + generate + serve)
    clean       Limpiar documentación generada
    stats       Mostrar estadísticas de documentación
    help        Mostrar esta ayuda

${GREEN}Ejemplos:${NC}
  
  ${BLUE}# Primera vez${NC}
  $0 build
  $0 config
  # Edita docs/Doxyfile si es necesario
  $0 generate
  $0 serve

  ${BLUE}# Flujo rápido${NC}
  $0 full

  ${BLUE}# Regenerar documentación${NC}
  $0 generate && $0 serve

${GREEN}Estructura del proyecto:${NC}
  ./src/          → Código fuente
  ./include/      → Headers
  ./docs/         → Configuración y documentación generada
  ./tests/        → Ejemplos (excluidos de docs)

${GREEN}Documentación:${NC}
  Ver en: http://localhost:$DOCS_PORT (después de ejecutar 'serve')

EOF
}

# Procesar comando
check_docker

case "$1" in
    build)
        build_image
        ;;
    config)
        generate_config
        ;;
    generate|gen)
        generate_docs
        ;;
    serve|server|start)
        serve_docs
        ;;
    stop)
        stop_server
        ;;
    clean)
        clean
        ;;
    stats)
        stats
        ;;
    full|all)
        full_workflow
        ;;
    help|--help|-h|"")
        show_help
        ;;
    *)
        print_error "Comando no reconocido: $1"
        echo ""
        show_help
        exit 1
        ;;
esac
