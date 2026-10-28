//! parser.rs - High-speed zero-copy OpenFOAM polyMesh and dictionary parser.

use crate::mesh::{FoamBoundaryPatch, FoamMesh};
use flate2::read::GzDecoder;
use memmap2::Mmap;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

/// Reads file contents either from raw file (via mmap) or decompresses on the fly if .gz
pub fn read_file_or_gz(path: &Path) -> Result<Vec<u8>, String> {
    let candidate = if path.exists() {
        path.to_path_buf()
    } else {
        let mut gz_path = path.as_os_str().to_os_string();
        gz_path.push(".gz");
        let p = PathBuf::from(gz_path);
        if p.exists() {
            p
        } else {
            return Err(format!("File does not exist: {}", path.display()));
        }
    };

    let is_gz = candidate.extension().and_then(|s| s.to_str()) == Some("gz");
    let file = File::open(&candidate).map_err(|e| format!("Failed to open {}: {}", candidate.display(), e))?;

    if is_gz {
        let mut gz = GzDecoder::new(file);
        let mut buffer = Vec::new();
        gz.read_to_end(&mut buffer)
            .map_err(|e| format!("Failed to decompress {}: {}", candidate.display(), e))?;
        Ok(buffer)
    } else {
        let mmap = unsafe {
            Mmap::map(&file).map_err(|e| format!("Failed to mmap {}: {}", candidate.display(), e))?
        };
        Ok(mmap.to_vec())
    }
}

/// Tokenizer that skips C/C++ style comments and OpenFOAM header
pub struct FoamStream<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> FoamStream<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, pos: 0 }
    }

    #[inline]
    fn peek(&self) -> Option<u8> {
        if self.pos < self.bytes.len() {
            Some(self.bytes[self.pos])
        } else {
            None
        }
    }

    #[inline]
    fn advance(&mut self) {
        if self.pos < self.bytes.len() {
            self.pos += 1;
        }
    }

    /// Skips whitespace, line comments (//) and block comments (/* ... */)
    pub fn skip_whitespace_and_comments(&mut self) {
        while self.pos < self.bytes.len() {
            let b = self.bytes[self.pos];
            if b.is_ascii_whitespace() {
                self.pos += 1;
            } else if b == b'/' {
                if self.pos + 1 < self.bytes.len() {
                    let next = self.bytes[self.pos + 1];
                    if next == b'/' {
                        // Skip line comment
                        self.pos += 2;
                        while self.pos < self.bytes.len() && self.bytes[self.pos] != b'\n' {
                            self.pos += 1;
                        }
                    } else if next == b'*' {
                        // Skip block comment
                        self.pos += 2;
                        while self.pos + 1 < self.bytes.len() {
                            if self.bytes[self.pos] == b'*' && self.bytes[self.pos + 1] == b'/' {
                                self.pos += 2;
                                break;
                            }
                            self.pos += 1;
                        }
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }

    /// Read next whitespace-delimited word
    pub fn next_token(&mut self) -> Option<&'a str> {
        self.skip_whitespace_and_comments();
        if self.pos >= self.bytes.len() {
            return None;
        }

        let start = self.pos;
        while self.pos < self.bytes.len() {
            let b = self.bytes[self.pos];
            if b.is_ascii_whitespace() || b == b';' || b == b'(' || b == b')' || b == b'{' || b == b'}' {
                if self.pos == start {
                    // Single character delimiter token
                    self.pos += 1;
                    return std::str::from_utf8(&self.bytes[start..self.pos]).ok();
                }
                break;
            }
            if b == b'/' && self.pos + 1 < self.bytes.len() {
                let next = self.bytes[self.pos + 1];
                if next == b'/' || next == b'*' {
                    break;
                }
            }
            self.pos += 1;
        }

        std::str::from_utf8(&self.bytes[start..self.pos]).ok()
    }

    /// Skips past FoamFile header { ... }
    pub fn skip_foam_header(&mut self) -> Result<(), String> {
        loop {
            match self.next_token() {
                Some("FoamFile") => break,
                Some(_) => continue,
                None => return Err("Could not find FoamFile header in OpenFOAM file".to_string()),
            }
        }

        // Expect '{'
        match self.next_token() {
            Some("{") => {}
            tok => return Err(format!("Expected '{{' after FoamFile, got {:?}", tok)),
        }

        let mut depth = 1;
        while depth > 0 {
            match self.next_token() {
                Some("{") => depth += 1,
                Some("}") => depth -= 1,
                Some(_) => {}
                None => return Err("Unclosed FoamFile header".to_string()),
            }
        }

        Ok(())
    }
}

/// Parses OpenFOAM `points` file
pub fn parse_points(bytes: &[u8]) -> Result<Vec<[f64; 3]>, String> {
    let mut stream = FoamStream::new(bytes);
    stream.skip_foam_header()?;

    // Read points count
    let count_str = stream.next_token().ok_or("Missing points count")?;
    let n_points: usize = count_str
        .parse()
        .map_err(|e| format!("Invalid points count '{}': {}", count_str, e))?;

    // Expect '('
    match stream.next_token() {
        Some("(") => {}
        tok => return Err(format!("Expected '(' before points list, got {:?}", tok)),
    }

    let mut points = Vec::with_capacity(n_points);

    for _ in 0..n_points {
        match stream.next_token() {
            Some("(") => {}
            tok => return Err(format!("Expected '(' starting point vector, got {:?}", tok)),
        }
        let x: f64 = stream.next_token().ok_or("Missing x coordinate")?.parse().map_err(|e| format!("Invalid x: {}", e))?;
        let y: f64 = stream.next_token().ok_or("Missing y coordinate")?.parse().map_err(|e| format!("Invalid y: {}", e))?;
        let z: f64 = stream.next_token().ok_or("Missing z coordinate")?.parse().map_err(|e| format!("Invalid z: {}", e))?;
        match stream.next_token() {
            Some(")") => {}
            tok => return Err(format!("Expected ')' ending point vector, got {:?}", tok)),
        }
        points.push([x, y, z]);
    }

    Ok(points)
}

/// Parses OpenFOAM `faces` file
pub fn parse_faces(bytes: &[u8]) -> Result<(Vec<usize>, Vec<usize>), String> {
    let mut stream = FoamStream::new(bytes);
    stream.skip_foam_header()?;

    let count_str = stream.next_token().ok_or("Missing faces count")?;
    let n_faces: usize = count_str
        .parse()
        .map_err(|e| format!("Invalid faces count '{}': {}", count_str, e))?;

    match stream.next_token() {
        Some("(") => {}
        tok => return Err(format!("Expected '(' before faces list, got {:?}", tok)),
    }

    let mut face_offsets = Vec::with_capacity(n_faces + 1);
    let mut face_nodes = Vec::with_capacity(n_faces * 4);
    face_offsets.push(0);

    for _ in 0..n_faces {
        let tok = stream.next_token().ok_or("Unexpected EOF reading face token")?;
        
        let (n_nodes, has_paren) = if tok.ends_with('(') {
            let num_part = &tok[..tok.len() - 1];
            let count: usize = num_part.parse().map_err(|e| format!("Invalid face node count in '{}': {}", tok, e))?;
            (count, true)
        } else {
            let count: usize = tok.parse().map_err(|e| format!("Invalid face node count '{}': {}", tok, e))?;
            (count, false)
        };

        if !has_paren {
            match stream.next_token() {
                Some("(") => {}
                other => return Err(format!("Expected '(' after face size {}, got {:?}", n_nodes, other)),
            }
        }

        for _ in 0..n_nodes {
            let node_idx: usize = stream
                .next_token()
                .ok_or("Missing node index in face")?
                .parse()
                .map_err(|e| format!("Invalid node index: {}", e))?;
            face_nodes.push(node_idx);
        }

        match stream.next_token() {
            Some(")") => {}
            other => return Err(format!("Expected ')' closing face, got {:?}", other)),
        }

        face_offsets.push(face_nodes.len());
    }

    Ok((face_offsets, face_nodes))
}

/// Parses OpenFOAM `owner` or `neighbour` labelList
pub fn parse_label_list(bytes: &[u8]) -> Result<Vec<usize>, String> {
    let mut stream = FoamStream::new(bytes);
    stream.skip_foam_header()?;

    let count_str = stream.next_token().ok_or("Missing label list count")?;
    let n_labels: usize = count_str
        .parse()
        .map_err(|e| format!("Invalid label list count '{}': {}", count_str, e))?;

    match stream.next_token() {
        Some("(") => {}
        tok => return Err(format!("Expected '(' before label list, got {:?}", tok)),
    }

    let mut labels = Vec::with_capacity(n_labels);
    for _ in 0..n_labels {
        let val_str = stream.next_token().ok_or("Unexpected EOF reading label value")?;
        let val: usize = val_str.parse().map_err(|e| format!("Invalid label integer '{}': {}", val_str, e))?;
        labels.push(val);
    }

    Ok(labels)
}

/// Parses OpenFOAM `boundary` file
pub fn parse_boundary(bytes: &[u8]) -> Result<Vec<FoamBoundaryPatch>, String> {
    let mut stream = FoamStream::new(bytes);
    stream.skip_foam_header()?;

    let count_str = stream.next_token().ok_or("Missing boundary count")?;
    let n_patches: usize = count_str
        .parse()
        .map_err(|e| format!("Invalid boundary count '{}': {}", count_str, e))?;

    match stream.next_token() {
        Some("(") => {}
        tok => return Err(format!("Expected '(' before boundary patches, got {:?}", tok)),
    }

    let mut patches = Vec::with_capacity(n_patches);

    for _ in 0..n_patches {
        let patch_name = stream.next_token().ok_or("Missing patch name")?.to_string();
        match stream.next_token() {
            Some("{") => {}
            tok => return Err(format!("Expected '{{' starting patch '{}', got {:?}", patch_name, tok)),
        }

        let mut patch_type = String::from("patch");
        let mut n_faces = 0;
        let mut start_face = 0;

        loop {
            let key = match stream.next_token() {
                Some("}") => break,
                Some(k) => k,
                None => return Err(format!("Unclosed patch dictionary for '{}'", patch_name)),
            };

            match key {
                "type" => {
                    patch_type = stream.next_token().ok_or("Missing patch type value")?.to_string();
                    if let Some(";") = stream.next_token() {}
                }
                "nFaces" => {
                    let val_str = stream.next_token().ok_or("Missing nFaces value")?;
                    n_faces = val_str.parse().map_err(|e| format!("Invalid nFaces '{}': {}", val_str, e))?;
                    if let Some(";") = stream.next_token() {}
                }
                "startFace" => {
                    let val_str = stream.next_token().ok_or("Missing startFace value")?;
                    start_face = val_str.parse().map_err(|e| format!("Invalid startFace '{}': {}", val_str, e))?;
                    if let Some(";") = stream.next_token() {}
                }
                _ => {
                    // Skip other keys until ';' or nested block
                    let mut tok = stream.next_token();
                    while let Some(t) = tok {
                        if t == ";" {
                            break;
                        }
                        if t == "{" {
                            let mut d = 1;
                            while d > 0 {
                                match stream.next_token() {
                                    Some("{") => d += 1,
                                    Some("}") => d -= 1,
                                    _ => {}
                                }
                            }
                            break;
                        }
                        tok = stream.next_token();
                    }
                }
            }
        }

        patches.push(FoamBoundaryPatch {
            name: patch_name,
            patch_type,
            start_face,
            n_faces,
        });
    }

    Ok(patches)
}

/// High-level function: loads a complete OpenFOAM polyMesh from directory
pub fn load_poly_mesh(poly_mesh_dir: &Path) -> Result<FoamMesh, String> {
    let points_raw = read_file_or_gz(&poly_mesh_dir.join("points"))?;
    let faces_raw = read_file_or_gz(&poly_mesh_dir.join("faces"))?;
    let owner_raw = read_file_or_gz(&poly_mesh_dir.join("owner"))?;
    let neighbour_raw = read_file_or_gz(&poly_mesh_dir.join("neighbour"))?;
    let boundary_raw = read_file_or_gz(&poly_mesh_dir.join("boundary"))?;

    let points = parse_points(&points_raw)?;
    let (face_offsets, face_nodes) = parse_faces(&faces_raw)?;
    let owner = parse_label_list(&owner_raw)?;
    let neighbour = parse_label_list(&neighbour_raw)?;
    let boundaries = parse_boundary(&boundary_raw)?;

    // Derive cell count from owner and neighbour
    let max_owner = owner.iter().cloned().max().unwrap_or(0);
    let max_neighbour = neighbour.iter().cloned().max().unwrap_or(0);
    let n_cells = max_owner.max(max_neighbour) + 1;

    let mut mesh = FoamMesh {
        points,
        face_offsets,
        face_nodes,
        owner,
        neighbour,
        n_cells,
        boundaries,
        ..Default::default()
    };

    mesh.validate_topology()?;
    mesh.compute_geometry();

    Ok(mesh)
}
