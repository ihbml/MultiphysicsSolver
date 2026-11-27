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
    pub is_binary: bool,
    pub label_bits: Option<u8>,
    pub class_name: String,
}

impl<'a> FoamStream<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self {
            bytes,
            pos: 0,
            is_binary: false,
            label_bits: None,
            class_name: String::new(),
        }
    }

    #[inline]
    pub fn is_binary(&self) -> bool {
        self.is_binary
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

    /// Read next whitespace-delimited word (keeps quoted strings intact without quotes).
    pub fn next_token(&mut self) -> Option<&'a str> {
        self.skip_whitespace_and_comments();
        if self.pos >= self.bytes.len() {
            return None;
        }

        if self.bytes[self.pos] == b'"' {
            self.pos += 1;
            let q_start = self.pos;
            while self.pos < self.bytes.len() && self.bytes[self.pos] != b'"' {
                self.pos += 1;
            }
            let q_end = self.pos;
            if self.pos < self.bytes.len() && self.bytes[self.pos] == b'"' {
                self.pos += 1;
            }
            return std::str::from_utf8(&self.bytes[q_start..q_end]).ok();
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

    /// Skips past FoamFile header { ... } and detects format binary;, arch, and class.
    pub fn skip_foam_header(&mut self) -> Result<(), String> {
        loop {
            match self.next_token() {
                Some("FoamFile") => break,
                Some(_) => continue,
                None => return Err("Could not find FoamFile header in OpenFOAM file".to_string()),
            }
        }

        match self.next_token() {
            Some("{") => {}
            tok => return Err(format!("Expected '{{' after FoamFile, got {:?}", tok)),
        }

        let mut depth = 1;
        while depth > 0 {
            match self.next_token() {
                Some("{") => depth += 1,
                Some("}") => depth -= 1,
                Some("format") => {
                    if let Some(fmt) = self.next_token() {
                        if fmt == "binary" {
                            self.is_binary = true;
                        }
                    }
                }
                Some("arch") => {
                    if let Some(arch) = self.next_token() {
                        if arch.contains("label=32") {
                            self.label_bits = Some(32);
                        } else if arch.contains("label=64") {
                            self.label_bits = Some(64);
                        }
                    }
                }
                Some("class") => {
                    if let Some(cls) = self.next_token() {
                        self.class_name = cls.to_string();
                    }
                }
                Some(_) => {}
                None => return Err("Unclosed FoamFile header".to_string()),
            }
        }

        Ok(())
    }

    fn consume_binary_open_paren(&mut self, expected_bytes: usize) -> Result<(), String> {
        self.skip_whitespace_and_comments();
        if self.pos < self.bytes.len() && self.bytes[self.pos] == b'(' {
            self.pos += 1;
        } else {
            return Err("Expected '(' before binary list payload".to_string());
        }
        // Standard OpenFOAM writes '<N>\n(<payload>)'.
        // Skip a single '\n' after '(' ONLY when legacy '\n(\n<payload>\n)' layout is present,
        // never when the first payload byte itself is 0x0A.
        if self.pos < self.bytes.len()
            && self.bytes[self.pos] == b'\n'
            && self.pos + 1 + expected_bytes < self.bytes.len()
            && self.bytes.get(self.pos + expected_bytes) != Some(&b')')
            && self.bytes.get(self.pos + 1 + expected_bytes..self.pos + 1 + expected_bytes + 2)
                == Some(b"\n)")
        {
            self.pos += 1;
        }
        Ok(())
    }

    /// Reads count f64 (double precision) binary values in little-endian format.
    pub fn read_binary_f64_slice(&mut self, count: usize) -> Result<Vec<f64>, String> {
        let byte_len = count * std::mem::size_of::<f64>();
        self.consume_binary_open_paren(byte_len)?;
        if self.pos + byte_len > self.bytes.len() {
            return Err(format!(
                "Unexpected EOF reading {} binary f64 values (need {} bytes, have {})",
                count,
                byte_len,
                self.bytes.len().saturating_sub(self.pos)
            ));
        }
        let slice = &self.bytes[self.pos..self.pos + byte_len];
        self.pos += byte_len;

        let mut result = Vec::with_capacity(count);
        for chunk in slice.chunks_exact(8) {
            let val = f64::from_le_bytes(chunk.try_into().unwrap());
            result.push(val);
        }

        self.skip_whitespace_and_comments();
        if self.pos < self.bytes.len() && self.bytes[self.pos] == b')' {
            self.pos += 1;
        }
        if self.pos < self.bytes.len() && self.bytes[self.pos] == b';' {
            self.pos += 1;
        }

        Ok(result)
    }

    /// Reads count 3D vectors ([f64; 3]) in binary little-endian format.
    pub fn read_binary_vector_slice(&mut self, count: usize) -> Result<Vec<[f64; 3]>, String> {
        let byte_len = count * 3 * std::mem::size_of::<f64>();
        self.consume_binary_open_paren(byte_len)?;
        if self.pos + byte_len > self.bytes.len() {
            return Err(format!(
                "Unexpected EOF reading {} binary vectors (need {} bytes, have {})",
                count,
                byte_len,
                self.bytes.len().saturating_sub(self.pos)
            ));
        }
        let slice = &self.bytes[self.pos..self.pos + byte_len];
        self.pos += byte_len;

        let mut result = Vec::with_capacity(count);
        for chunk in slice.chunks_exact(24) {
            let x = f64::from_le_bytes(chunk[0..8].try_into().unwrap());
            let y = f64::from_le_bytes(chunk[8..16].try_into().unwrap());
            let z = f64::from_le_bytes(chunk[16..24].try_into().unwrap());
            result.push([x, y, z]);
        }

        self.skip_whitespace_and_comments();
        if self.pos < self.bytes.len() && self.bytes[self.pos] == b')' {
            self.pos += 1;
        }
        if self.pos < self.bytes.len() && self.bytes[self.pos] == b';' {
            self.pos += 1;
        }

        Ok(result)
    }

    /// Reads count labels (integers) in binary little-endian format (supports 32-bit and 64-bit).
    pub fn read_binary_label_slice(&mut self, count: usize) -> Result<Vec<usize>, String> {
        let byte_len_64 = count * 8;
        let byte_len_32 = count * 4;
        self.skip_whitespace_and_comments();
        let after_paren = if self.pos < self.bytes.len() && self.bytes[self.pos] == b'(' {
            self.pos + 1
        } else {
            return Err("Expected '(' before binary label list".to_string());
        };
        let remaining = self.bytes.len().saturating_sub(after_paren);
        let is_64 = match self.label_bits {
            Some(32) => false,
            Some(64) => true,
            _ => remaining >= byte_len_64,
        };
        let byte_len = if is_64 { byte_len_64 } else { byte_len_32 };
        self.consume_binary_open_paren(byte_len)?;
        if self.pos + byte_len > self.bytes.len() {
            return Err(format!("Unexpected EOF reading {} binary labels", count));
        }
        let slice = &self.bytes[self.pos..self.pos + byte_len];
        self.pos += byte_len;
        let mut result = Vec::with_capacity(count);
        if is_64 {
            for chunk in slice.chunks_exact(8) {
                let val = i64::from_le_bytes(chunk.try_into().unwrap()) as usize;
                result.push(val);
            }
        } else {
            for chunk in slice.chunks_exact(4) {
                let val = i32::from_le_bytes(chunk.try_into().unwrap()) as usize;
                result.push(val);
            }
        }
        self.skip_whitespace_and_comments();
        if self.pos < self.bytes.len() && self.bytes[self.pos] == b')' {
            self.pos += 1;
        }
        if self.pos < self.bytes.len() && self.bytes[self.pos] == b';' {
            self.pos += 1;
        }
        Ok(result)
    }
}

/// Parses OpenFOAM `points` file (ASCII and Binary)
pub fn parse_points(bytes: &[u8]) -> Result<Vec<[f64; 3]>, String> {
    let mut stream = FoamStream::new(bytes);
    stream.skip_foam_header()?;

    let count_str = stream.next_token().ok_or("Missing points count")?;
    let n_points: usize = count_str
        .parse()
        .map_err(|e| format!("Invalid points count '{}': {}", count_str, e))?;

    if stream.is_binary() {
        return stream.read_binary_vector_slice(n_points);
    }

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

/// Parses OpenFOAM `faces` file (ASCII `faceList` and binary `faceCompactList`).
pub fn parse_faces(bytes: &[u8]) -> Result<(Vec<usize>, Vec<usize>), String> {
    let mut stream = FoamStream::new(bytes);
    stream.skip_foam_header()?;

    if stream.is_binary() && stream.class_name == "faceCompactList" {
        let fo_count_str = stream.next_token().ok_or("Missing face_offsets count")?;
        let n_fo: usize = fo_count_str
            .parse()
            .map_err(|e| format!("Invalid face_offsets count '{}': {}", fo_count_str, e))?;
        let face_offsets = stream.read_binary_label_slice(n_fo)?;

        let fn_count_str = stream.next_token().ok_or("Missing face_nodes count")?;
        let n_fn: usize = fn_count_str
            .parse()
            .map_err(|e| format!("Invalid face_nodes count '{}': {}", fn_count_str, e))?;
        let face_nodes = stream.read_binary_label_slice(n_fn)?;
        return Ok((face_offsets, face_nodes));
    }

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

/// Parses OpenFOAM `owner` or `neighbour` labelList (ASCII and Binary)
pub fn parse_label_list(bytes: &[u8]) -> Result<Vec<usize>, String> {
    let mut stream = FoamStream::new(bytes);
    stream.skip_foam_header()?;

    let count_str = stream.next_token().ok_or("Missing label list count")?;
    let n_labels: usize = count_str
        .parse()
        .map_err(|e| format!("Invalid label list count '{}': {}", count_str, e))?;

    if stream.is_binary() {
        return stream.read_binary_label_slice(n_labels);
    }

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
    mesh.sync_i64_indices();

    Ok(mesh)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_binary_points_preserves_0x0a_and_0x20_leading_bytes() {
        // Construct f64 values whose first LE bytes are literally 0x0A ('\n') and 0x20 (' ')
        let x0 = f64::from_le_bytes([0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F]); // ~1.0
        let y0 = f64::from_le_bytes([0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40]); // 2.0
        let z0 = 3.5_f64;

        let mut payload = Vec::new();
        payload.extend_from_slice(
            b"FoamFile\n{\n    version 2.0;\n    format binary;\n    arch \"LSB;label=64;scalar=64\";\n    class vectorField;\n    object points;\n}\n1\n(",
        );
        payload.extend_from_slice(&x0.to_le_bytes());
        payload.extend_from_slice(&y0.to_le_bytes());
        payload.extend_from_slice(&z0.to_le_bytes());
        payload.extend_from_slice(b")\n");

        let pts = parse_points(&payload).expect("binary points with 0x0a/0x20 byte");
        assert_eq!(pts.len(), 1);
        assert_eq!(pts[0][0].to_bits(), x0.to_bits());
        assert_eq!(pts[0][1].to_bits(), y0.to_bits());
        assert_eq!(pts[0][2].to_bits(), z0.to_bits());
    }

    #[test]
    fn test_binary_label_list_32_vs_64() {
        let mut payload32 = Vec::new();
        payload32.extend_from_slice(
            b"FoamFile { version 2.0; format binary; arch \"LSB;label=32;scalar=64\"; class labelList; object owner; }\n3\n(",
        );
        for v in [0i32, 1i32, 2i32] {
            payload32.extend_from_slice(&v.to_le_bytes());
        }
        payload32.extend_from_slice(b")\n");
        assert_eq!(parse_label_list(&payload32).unwrap(), vec![0, 1, 2]);
    }
}
